// bench.h - load-generation engine for the dashboard. Header-only, no ImGui dependency.
//
// Runs N worker threads, each with its own TCP connection, against the Redis clone and records
// per-operation latency. Runs on a background thread, so the UI never blocks.
#pragma once
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace bench {

enum Workload { W_PING, W_SET, W_GET, W_MIXED, W_RPUSH, W_HSET, W_SADD, W_ZADD, W_COUNT };
inline const char *const WORKLOAD_NAMES[W_COUNT] = {"PING", "SET", "GET", "MIXED (80% GET / 20% SET)", "RPUSH", "HSET", "SADD", "ZADD"};
inline const char *const KEY_TAG[W_COUNT] = {"ping", "set", "get", "mix", "rpush", "hset", "sadd", "zadd"};

struct Config {
    std::string host = "127.0.0.1", port = "6379";
    int workload = W_SET, threads = 4, payload = 32, keyspace = 1000, total_ops = 20000;
    bool reconnect = false; // new TCP connection per operation (includes connect cost)
    bool cleanup = true;    // DEL the bench:* keys afterwards
};

struct Result {
    uint64_t run_id = 0;
    bool running = false, done = false, reconnect = false;
    std::string status = "idle", workload;
    int threads = 0;
    uint64_t total = 0, completed = 0, ok = 0, errors = 0, unframed = 0;
    double seconds = 0, ops_per_sec = 0;
    double min_us = 0, avg_us = 0, p50_us = 0, p90_us = 0, p95_us = 0, p99_us = 0, p999_us = 0, max_us = 0;
    std::vector<float> series;      // ops/sec sampled every 100 ms
    std::array<uint32_t, 20> hist{}; // bucket i = latencies in [2^i, 2^(i+1)) microseconds
};

using Clock = std::chrono::steady_clock;

inline std::string encode(const std::vector<std::string> &a) {
    std::string o = "*" + std::to_string(a.size()) + "\r\n";
    for (const auto &s : a) o += "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
    return o;
}

// true once a complete RESP reply starting at `pos` is in `b`; advances pos past it.
inline bool complete(const std::string &b, size_t &pos) {
    if (pos >= b.size()) return false;
    char t = b[pos];
    size_t eol = b.find("\r\n", pos);
    if (eol == std::string::npos) return false;
    size_t next = eol + 2;
    if (t == '+' || t == '-' || t == ':') { pos = next; return true; }
    if (t == '$') {
        long len = std::strtol(b.c_str() + pos + 1, nullptr, 10);
        if (len < 0) { pos = next; return true; }
        if (b.size() < next + (size_t)len + 2) return false;
        pos = next + len + 2;
        return true;
    }
    if (t == '*') {
        long n = std::strtol(b.c_str() + pos + 1, nullptr, 10);
        size_t p = next;
        for (long i = 0; i < n; i++)
            if (!complete(b, p)) return false;
        pos = p;
        return true;
    }
    pos = b.size(); // unknown prefix: accept what we have
    return true;
}

class Conn {
public:
    Conn(std::string h, std::string p) : host_(std::move(h)), port_(std::move(p)) {}
    ~Conn() { close(); }
    Conn(const Conn &) = delete;
    Conn &operator=(const Conn &) = delete;

    bool isOpen() const { return fd_ >= 0; }
    void close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

    bool open() {
        close();
        if (!resolved_) {
            addrinfo hints{}, *res = nullptr;
            hints.ai_socktype = SOCK_STREAM;
            if (getaddrinfo(host_.c_str(), port_.c_str(), &hints, &res) != 0 || !res) return false;
            family_ = res->ai_family; proto_ = res->ai_protocol; alen_ = res->ai_addrlen;
            std::memcpy(&addr_, res->ai_addr, res->ai_addrlen);
            freeaddrinfo(res);
            resolved_ = true;
        }
        fd_ = ::socket(family_, SOCK_STREAM, proto_);
        if (fd_ < 0) return false;
        if (::connect(fd_, (sockaddr *)&addr_, alen_) != 0) { close(); return false; }
        int one = 1;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        return true;
    }

    // One request/response round trip on the current connection.
    bool request(const std::string &wire, bool &isErr, bool &unframed) {
        if (fd_ < 0) return false;
        size_t sent = 0;
        while (sent < wire.size()) {
            ssize_t n = ::send(fd_, wire.data() + sent, wire.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += (size_t)n;
        }
        rbuf_.clear();
        char tmp[4096];
        auto deadline = Clock::now() + std::chrono::seconds(3);
        for (;;) {
            size_t pos = 0;
            if (complete(rbuf_, pos)) { isErr = rbuf_[0] == '-'; return true; }
            int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (left <= 0) return false;
            pollfd p{fd_, POLLIN, 0};
            int rc = ::poll(&p, 1, rbuf_.empty() ? left : std::min(left, 150));
            if (rc == 0) {
                if (rbuf_.empty()) continue;
                unframed = true; // reply without CRLF: accepted after an idle gap (inflates latency)
                isErr = rbuf_[0] == '-';
                return true;
            }
            if (rc < 0) return false;
            ssize_t n = ::recv(fd_, tmp, sizeof tmp, 0);
            if (n <= 0) return false;
            rbuf_.append(tmp, (size_t)n);
        }
    }

    // request(), reconnecting once if the server closed the connection after the previous reply.
    bool call(const std::string &wire, bool &isErr, bool &unframed) {
        if (fd_ >= 0 && request(wire, isErr, unframed)) return true;
        isErr = unframed = false;
        return open() && request(wire, isErr, unframed);
    }

private:
    std::string host_, port_, rbuf_;
    int fd_ = -1, family_ = 0, proto_ = 0;
    sockaddr_storage addr_{};
    socklen_t alen_ = 0;
    bool resolved_ = false;
};

class Runner {
public:
    ~Runner() { cancel_ = true; if (th_.joinable()) th_.join(); }

    bool isRunning() const { return running_.load(); }
    void stop() { cancel_ = true; }
    Result snapshot() { std::lock_guard<std::mutex> lk(m_); return res_; }

    bool start(Config c) {
        if (running_) return false;
        if (th_.joinable()) th_.join();
        c.threads = std::clamp(c.threads, 1, 256);
        c.total_ops = std::clamp(c.total_ops, 1, 5000000);
        c.payload = std::clamp(c.payload, 0, 1 << 20);
        c.keyspace = std::clamp(c.keyspace, 1, 100000);
        c.workload = std::clamp(c.workload, 0, (int)W_COUNT - 1);
        {
            std::lock_guard<std::mutex> lk(m_);
            Result r;
            r.run_id = ++runId_; r.running = true; r.status = "starting";
            r.workload = WORKLOAD_NAMES[c.workload]; r.threads = c.threads;
            r.total = (uint64_t)c.total_ops; r.reconnect = c.reconnect;
            res_ = std::move(r);
        }
        cancel_ = false;
        running_ = true;
        th_ = std::thread([this, c] { run(c); });
        return true;
    }

private:
    std::mutex m_;
    Result res_;
    std::thread th_;
    std::atomic<bool> cancel_{false}, running_{false};
    uint64_t runId_ = 0;

    void setStatus(const char *s) { std::lock_guard<std::mutex> lk(m_); res_.status = s; }

    static std::string keyOf(int w, uint64_t i) { return std::string("bench:") + KEY_TAG[w] + ":" + std::to_string(i); }

    static std::string makeCmd(const Config &c, std::mt19937_64 &rng, const std::string &payload) {
        auto key = [&](int w) { return keyOf(w, rng() % (uint64_t)c.keyspace); };
        switch (c.workload) {
        case W_PING: return encode({"PING"});
        case W_SET: return encode({"SET", key(W_SET), payload});
        case W_GET: return encode({"GET", key(W_GET)});
        case W_MIXED: return rng() % 10 < 8 ? encode({"GET", key(W_MIXED)}) : encode({"SET", key(W_MIXED), payload});
        case W_RPUSH: return encode({"RPUSH", key(W_RPUSH), payload});
        case W_HSET: return encode({"HSET", key(W_HSET), "f" + std::to_string(rng() % 16), payload});
        case W_SADD: return encode({"SADD", key(W_SADD), "m" + std::to_string(rng() % 1000)});
        default: return encode({"ZADD", key(W_ZADD), std::to_string(rng() % 1000), "m" + std::to_string(rng() % 1000)});
        }
    }

    struct Out { std::vector<uint32_t> lat; uint64_t err = 0, unf = 0; };

    void run(const Config c) {
        const uint64_t total = (uint64_t)c.total_ops;
        bool e, u;

        if (c.workload == W_GET || c.workload == W_MIXED) { // GET needs existing keys
            setStatus("preloading keys");
            Conn pc(c.host, c.port);
            std::string v((size_t)c.payload, 'x');
            for (int i = 0; i < c.keyspace && !cancel_; i++) pc.call(encode({"SET", keyOf(c.workload, (uint64_t)i), v}), e, u);
        }

        setStatus("running");
        std::atomic<uint64_t> next{0}, done{0};
        std::atomic<int> finished{0};
        std::vector<Out> outs((size_t)c.threads);
        std::vector<std::thread> ws;
        auto t0 = Clock::now();

        for (int t = 0; t < c.threads; t++)
            ws.emplace_back([&, t] {
                Out &out = outs[(size_t)t];
                Conn conn(c.host, c.port);
                if (!c.reconnect) conn.open();
                std::mt19937_64 rng(std::random_device{}() ^ ((uint64_t)t << 32));
                std::string payload((size_t)c.payload, 'x');
                out.lat.reserve(total / (uint64_t)c.threads + 16);
                while (!cancel_) {
                    if (next.fetch_add(1, std::memory_order_relaxed) >= total) break;
                    std::string wire = makeCmd(c, rng, payload); // built outside the timed section
                    bool isErr = false, un = false, good;
                    auto a = Clock::now();
                    if (c.reconnect) good = conn.open() && conn.request(wire, isErr, un);
                    else good = conn.call(wire, isErr, un);
                    auto b = Clock::now();
                    if (un) out.unf++;
                    if (!good || isErr) out.err++;
                    else out.lat.push_back((uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(b - a).count());
                    done.fetch_add(1, std::memory_order_relaxed);
                }
                finished.fetch_add(1);
            });

        uint64_t last = 0;
        while (finished.load() < c.threads) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            uint64_t d = done.load();
            std::lock_guard<std::mutex> lk(m_);
            res_.series.push_back((float)(d - last) * 10.f);
            res_.completed = d;
            last = d;
        }
        for (auto &w : ws) w.join();
        double secs = std::chrono::duration<double>(Clock::now() - t0).count();

        std::vector<uint32_t> all;
        uint64_t err = 0, unf = 0;
        for (auto &o : outs) { all.insert(all.end(), o.lat.begin(), o.lat.end()); err += o.err; unf += o.unf; }
        std::sort(all.begin(), all.end());
        {
            std::lock_guard<std::mutex> lk(m_);
            res_.completed = done.load();
            res_.ok = all.size(); res_.errors = err; res_.unframed = unf; res_.seconds = secs;
            res_.ops_per_sec = secs > 0 ? (double)all.size() / secs : 0;
            if (!all.empty()) {
                auto pct = [&](double q) { return (double)all[std::min(all.size() - 1, (size_t)(q * (double)(all.size() - 1) + 0.5))]; };
                double sum = 0;
                for (uint32_t v : all) {
                    sum += v;
                    res_.hist[(size_t)std::min(19, 63 - __builtin_clzll((uint64_t)v | 1))]++;
                }
                res_.min_us = all.front(); res_.max_us = all.back(); res_.avg_us = sum / (double)all.size();
                res_.p50_us = pct(.50); res_.p90_us = pct(.90); res_.p95_us = pct(.95); res_.p99_us = pct(.99); res_.p999_us = pct(.999);
            }
            res_.status = cancel_ ? "stopped" : "done";
        }

        if (c.cleanup && c.workload != W_PING) {
            setStatus("cleaning up");
            Conn cc(c.host, c.port);
            for (int i = 0; i < c.keyspace; i++) cc.call(encode({"DEL", keyOf(c.workload, (uint64_t)i)}), e, u);
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            res_.status = cancel_ ? "stopped" : "done";
            res_.running = false; res_.done = true;
        }
        running_ = false;
    }
};

inline Runner &runner() { static Runner r; return r; }

} // namespace bench
