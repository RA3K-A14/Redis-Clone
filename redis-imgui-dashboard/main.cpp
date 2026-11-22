// Redis-Clone desktop dashboard — Dear ImGui + GLFW + OpenGL3. Talks RESP over TCP directly
// to your server (no bridge needed). Network I/O runs on a worker thread so the UI never blocks.
//
// Usage: redis_dashboard [host] [port]     (or env REDIS_HOST / REDIS_PORT; optional DASH_FONT=/path/font.ttf)

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define CLOSESOCK close
#define POLLFN poll
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "bench.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
static std::string REDIS_HOST = "127.0.0.1", REDIS_PORT = "6379";
static const size_t MAX_KEYS = 500;

// ======================= RESP client =======================
struct Reply
{
    std::string type = "error"; // simple | error | int | bulk | nil | array
    std::string str;
    long long num = 0;
    std::vector<std::string> arr;
};

static std::string encode(const std::vector<std::string> &args)
{
    std::string out = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto &a : args)
        out += "$" + std::to_string(a.size()) + "\r\n" + a + "\r\n";
    return out;
}

static bool parseOne(const std::string &buf, size_t &pos, Reply &r)
{
    if (pos >= buf.size())
        return false;
    char t = buf[pos];
    size_t eol = buf.find("\r\n", pos);
    if (eol == std::string::npos)
        return false;
    std::string line = buf.substr(pos + 1, eol - pos - 1);
    size_t next = eol + 2;
    if (t == '+')
    {
        r.type = "simple";
        r.str = line;
        pos = next;
        return true;
    }
    if (t == '-')
    {
        r.type = "error";
        r.str = line;
        pos = next;
        return true;
    }
    if (t == ':')
    {
        r.type = "int";
        r.num = std::strtoll(line.c_str(), nullptr, 10);
        pos = next;
        return true;
    }
    if (t == '$')
    {
        long len = std::strtol(line.c_str(), nullptr, 10);
        if (len < 0)
        {
            r.type = "nil";
            pos = next;
            return true;
        }
        if (buf.size() < next + (size_t)len + 2)
            return false;
        r.type = "bulk";
        r.str = buf.substr(next, len);
        pos = next + len + 2;
        return true;
    }
    if (t == '*')
    {
        long n = std::strtol(line.c_str(), nullptr, 10);
        size_t p = next;
        Reply out;
        out.type = "array";
        for (long i = 0; i < n; i++)
        {
            Reply e;
            if (!parseOne(buf, p, e))
                return false;
            out.arr.push_back(e.type == "int" ? std::to_string(e.num) : e.str);
        }
        r = out;
        pos = p;
        return true;
    }
    r.type = "simple";
    r.str = buf.substr(pos);
    while (!r.str.empty() && (r.str.back() == '\n' || r.str.back() == '\r'))
        r.str.pop_back();
    pos = buf.size();
    return true;
}

// The clone's replies are not always CRLF-terminated, so after an idle gap we take what we have.
static Reply settle(std::string buf)
{
    size_t pos = 0;
    Reply r;
    if (parseOne(buf, pos, r))
        return r;
    while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r'))
        buf.pop_back();
    if (buf.empty())
    {
        r.type = "error";
        r.str = "empty reply";
        return r;
    }
    if (buf[0] == '-')
    {
        r.type = "error";
        r.str = buf.substr(1);
    }
    else
    {
        r.type = "simple";
        r.str = buf[0] == '+' ? buf.substr(1) : buf;
    }
    return r;
}

static Reply sendCmd(const std::vector<std::string> &args)
{
    Reply fail;
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(REDIS_HOST.c_str(), REDIS_PORT.c_str(), &hints, &res) != 0)
    {
        fail.str = "cannot resolve host";
        return fail;
    }
    sock_t fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == (sock_t)-1 || connect(fd, res->ai_addr, (int)res->ai_addrlen) != 0)
    {
        if (fd != (sock_t)-1)
            CLOSESOCK(fd);
        freeaddrinfo(res);
        fail.str = "connection refused";
        return fail;
    }
    freeaddrinfo(res);
    std::string req = encode(args);
    size_t sent = 0;
    while (sent < req.size())
    {
        int n = (int)send(fd, req.data() + sent, (int)(req.size() - sent), 0);
        if (n <= 0)
        {
            CLOSESOCK(fd);
            fail.str = "write failed";
            return fail;
        }
        sent += n;
    }
    std::string buf;
    auto deadline = Clock::now() + std::chrono::seconds(4);
    Reply out;
    while (true)
    {
        int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0)
        {
            CLOSESOCK(fd);
            fail.str = "timeout";
            return fail;
        }
        pollfd p{};
        p.fd = fd;
        p.events = POLLIN;
        int rc = POLLFN(&p, 1, buf.empty() ? left : std::min(left, 300));
        if (rc == 0)
        {
            if (buf.empty())
                continue;
            break;
        }
        if (rc < 0)
            break;
        char tmp[4096];
        int n = (int)recv(fd, tmp, sizeof tmp, 0);
        if (n <= 0)
            break;
        buf.append(tmp, n);
        size_t pos = 0;
        if (parseOne(buf, pos, out))
        {
            CLOSESOCK(fd);
            return out;
        }
    }
    CLOSESOCK(fd);
    return settle(buf);
}

static bool isNil(const Reply &r) { return r.type == "nil" || (r.type == "error" && r.str == "1"); }

static std::string fmtReply(const Reply &r)
{
    if (isNil(r))
        return "(nil)";
    if (r.type == "error")
        return "(error) " + r.str;
    if (r.type == "int")
        return "(integer) " + std::to_string(r.num);
    if (r.type == "bulk")
        return "\"" + r.str + "\"";
    if (r.type == "array")
    {
        if (r.arr.empty())
            return "(empty array)";
        std::string o;
        for (size_t i = 0; i < r.arr.size(); i++)
            o += (i ? "\n" : "") + std::to_string(i + 1) + ") \"" + r.arr[i] + "\"";
        return o;
    }
    return r.str;
}

// Splits a console line on spaces, honouring "double" and 'single' quotes.
static std::vector<std::string> words(const std::string &s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && isspace((unsigned char)s[i]))
            i++;
        if (i >= s.size())
            break;
        std::string w;
        if (s[i] == '"' || s[i] == '\'')
        {
            char q = s[i++];
            while (i < s.size() && s[i] != q)
                w += s[i++];
            i++;
        }
        else
            while (i < s.size() && !isspace((unsigned char)s[i]))
                w += s[i++];
        out.push_back(w);
    }
    return out;
}

// ======================= shared state (worker <-> UI) =======================
struct KeyInfo
{
    std::string key, type;
    long long items = 1;
};
struct Overview
{
    bool online = false;
    std::string error = "connecting...";
    long ping_ms = 0;
    size_t total = 0;
    bool truncated = false;
    std::map<std::string, int> counts;
    std::vector<KeyInfo> keys;
};
struct Inspect
{
    std::string key, type;
    std::vector<std::string> data;
    std::vector<std::pair<std::string, std::string>> pairs;
};
struct Job
{
    std::vector<std::string> args;
    std::string tag;
};
struct LogEntry
{
    std::string cmd, out;
    bool err;
};

static std::mutex mu;
static std::condition_variable cv;
static bool quitting = false, refreshNow = true;
static std::vector<Job> jobs;
static std::vector<LogEntry> logv;
static Overview ov;
static Inspect ins;
static std::string wantKey, lastSave = "no SAVE from this app yet";

static void submit(std::vector<std::string> args, std::string tag = "")
{
    std::lock_guard<std::mutex> lk(mu);
    jobs.push_back({std::move(args), std::move(tag)});
    cv.notify_all();
}
static void setWantKey(const std::string &k)
{
    std::lock_guard<std::mutex> lk(mu);
    wantKey = k;
    refreshNow = true;
    cv.notify_all();
}

static long long toLL(const Reply &r)
{
    if (r.type == "int")
        return r.num;
    if (r.type == "error" || r.type == "nil")
        return 0;
    return std::strtoll(r.str.c_str(), nullptr, 10);
}

// Normalise whatever the server calls a type to the names the UI uses.
static std::string normType(std::string s)
{
    while (!s.empty() && isspace((unsigned char)s.back()))
        s.pop_back();
    size_t b = 0;
    while (b < s.size() && (isspace((unsigned char)s[b]) || s[b] == '+' || s[b] == '"'))
        b++;
    s = s.substr(b);
    if (!s.empty() && s.back() == '"')
        s.pop_back();
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s.empty())
        return "unknown";
    return s;
}

static KeyInfo describe(const std::string &key)
{
    KeyInfo k;
    k.key = key;
    Reply t = sendCmd({"TYPE", key});
    k.type = (t.type == "error" || t.type == "nil") ? "unknown" : normType(t.str);

    const char *lenCmd = k.type == "list"   ? "LLEN"
                         : k.type == "hash" ? "HLEN"
                         : k.type == "set"  ? "SCARD"
                                            : nullptr;
    if (lenCmd)
    {
        k.items = toLL(sendCmd({lenCmd, key}));
    }
    else if (k.type == "sorted_set")
    {
        Reply r = sendCmd({"ZALL", key});
        k.items = r.type == "array" ? (long long)r.arr.size() : 0; // halve this if ZALL returns member+score pairs
    }
    return k;
}

static Overview fetchOverview()
{
    Overview o;
    auto t0 = Clock::now();
    Reply ping = sendCmd({"PING"});
    o.ping_ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    if (ping.type == "error")
    {
        o.error = ping.str;
        return o;
    }
    o.online = true;
    Reply k = sendCmd({"KEYS"});
    std::vector<std::string> all;
    if (k.type == "array")
    {
        for (auto &x : k.arr)
        {
            // server returns "<T> <name>" where T is K/L/H/S/Z
            if (x.size() > 2 && x[1] == ' ' && std::string("KLHSZ").find(x[0]) != std::string::npos)
                all.push_back(x.substr(2));
            else
                all.push_back(x);
        }
    }
    o.total = all.size();
    o.truncated = all.size() > MAX_KEYS;
    size_t n = std::min(all.size(), MAX_KEYS);
    const size_t BATCH = 16;
    for (size_t base = 0; base < n; base += BATCH)
    {
        std::vector<std::future<KeyInfo>> fut;
        for (size_t i = base; i < std::min(n, base + BATCH); i++)
            fut.push_back(std::async(std::launch::async, describe, all[i]));
        for (auto &f : fut)
        {
            KeyInfo ki = f.get();
            o.counts[ki.type]++;
            o.keys.push_back(ki);
        }
    }
    return o;
}

static Inspect fetchInspect(const std::string &key)
{
    Inspect d;
    d.key = key;
    KeyInfo k = describe(key);
    d.type = k.type;
    const char *cmd = k.type == "string" ? "GET" : k.type == "list"     ? "LGET"
                                               : k.type == "hash"       ? "HGETALL"
                                               : k.type == "set"        ? "SMEMBERS"
                                               : k.type == "sorted_set" ? "ZALL"
                                                                        : nullptr;
    if (!cmd)
        return d;
    Reply r = sendCmd({cmd, key});
    if (isNil(r))
        return d;
    if (r.type != "array")
    {
        d.data.push_back(r.type == "int" ? std::to_string(r.num) : r.str);
        return d;
    }
    if (k.type == "hash")
        for (size_t i = 0; i + 1 < r.arr.size(); i += 2)
            d.pairs.push_back({r.arr[i], r.arr[i + 1]});
    else
        d.data = r.arr;
    return d;
}

static void worker()
{
    while (true)
    {
        std::vector<Job> mine;
        std::string key;
        {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait_for(lk, std::chrono::seconds(3), []
                        { return quitting || refreshNow || !jobs.empty(); });
            if (quitting)
                return;
            mine.swap(jobs);
            refreshNow = false;
            key = wantKey;
        }
        for (auto &j : mine)
        {
            Reply r = sendCmd(j.args);
            std::string line;
            for (size_t i = 0; i < j.args.size(); i++)
                line += (i ? " " : "") + j.args[i];
            std::lock_guard<std::mutex> lk(mu);
            logv.push_back({line, fmtReply(r), r.type == "error" && !isNil(r)});
            if (j.tag == "SAVE" && r.type == "simple")
            {
                char b[32];
                std::time_t t = std::time(nullptr);
                std::strftime(b, sizeof b, "%H:%M:%S", std::localtime(&t));
                lastSave = std::string("last SAVE ") + b;
            }
        }
        if (bench::runner().isRunning()) continue;
        Overview o = fetchOverview();
        Inspect d;
        if (!key.empty() && o.online)
            d = fetchInspect(key);
        std::lock_guard<std::mutex> lk(mu);
        ov = std::move(o);
        ins = std::move(d);
    }
}

// ======================= UI =======================
static ImVec4 hex(unsigned c, float a = 1.f) { return ImVec4(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, a); }
static const unsigned C_RED = 0xFF5A4E, C_OK = 0x3DD6A8, C_MUTED = 0x9DADA5, C_DANGER = 0xFF8A80;
static unsigned typeColor(const std::string &t)
{
    if (t == "string")
        return 0x3DD6A8;
    if (t == "hash")
        return 0xE8B04A;
    if (t == "list")
        return 0x8FA7FF;
    if (t == "set")
        return 0xFF9EC0;
    if (t == "sorted_set")
        return 0xC9A2FF;
    return C_MUTED;
}

static void applyTheme()
{
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 0;
    s.ChildRounding = 10;
    s.FrameRounding = 8;
    s.PopupRounding = 10;
    s.GrabRounding = 6;
    s.WindowPadding = ImVec2(18, 16);
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 10);
    s.CellPadding = ImVec2(8, 6);
    ImVec4 *c = s.Colors;
    c[ImGuiCol_Text] = hex(0xE6ECE8);
    c[ImGuiCol_TextDisabled] = hex(C_MUTED);
    c[ImGuiCol_WindowBg] = hex(0x0E1311);
    c[ImGuiCol_ChildBg] = hex(0x121A17);
    c[ImGuiCol_PopupBg] = hex(0x121A17);
    c[ImGuiCol_Border] = hex(0x1E2823);
    c[ImGuiCol_FrameBg] = hex(0x0A0F0D);
    c[ImGuiCol_FrameBgHovered] = hex(0x14201B);
    c[ImGuiCol_FrameBgActive] = hex(0x18241F);
    c[ImGuiCol_Button] = hex(0x18221E);
    c[ImGuiCol_ButtonHovered] = hex(0x22302A);
    c[ImGuiCol_ButtonActive] = hex(0x2A3A33);
    c[ImGuiCol_Header] = hex(0x18221E);
    c[ImGuiCol_HeaderHovered] = hex(0x22302A);
    c[ImGuiCol_HeaderActive] = hex(0x2A3A33);
    c[ImGuiCol_TableHeaderBg] = hex(0x121A17);
    c[ImGuiCol_TableRowBgAlt] = hex(0xFFFFFF, 0.02f);
    c[ImGuiCol_TableBorderLight] = hex(0x1E2823);
    c[ImGuiCol_TableBorderStrong] = hex(0x1E2823);
    c[ImGuiCol_ScrollbarBg] = hex(0x0A0F0D);
    c[ImGuiCol_ScrollbarGrab] = hex(0x2A3830);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
}

static bool primaryBtn(const char *l, ImVec2 sz = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button, hex(C_RED));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hex(0xFF7469));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, hex(0xE04C41));
    ImGui::PushStyleColor(ImGuiCol_Text, hex(0x1A0705));
    bool r = ImGui::Button(l, sz);
    ImGui::PopStyleColor(4);
    return r;
}
static bool dangerBtn(const char *l, ImVec2 sz = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button, hex(0x2A1512));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hex(0x3A1B17));
    ImGui::PushStyleColor(ImGuiCol_Text, hex(C_DANGER));
    bool r = ImGui::Button(l, sz);
    ImGui::PopStyleColor(3);
    return r;
}

#include "bench_panel.h"

struct Modal
{
    bool open = false, input = false;
    std::string title, msg, label;
    char buf[512] = {};
    std::function<void(const std::string &)> ok;
} modal;
static void ask(const std::string &title, const std::string &msg, bool input, const std::string &label,
                std::function<void(const std::string &)> ok)
{
    modal.open = true;
    modal.title = title;
    modal.msg = msg;
    modal.input = input;
    modal.label = label;
    modal.ok = std::move(ok);
    modal.buf[0] = 0;
}
static void drawModal()
{
    if (modal.open)
    {
        ImGui::OpenPopup("###modal");
        modal.open = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal((modal.title + "###modal").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushTextWrapPos(420);
        ImGui::TextUnformatted(modal.msg.c_str());
        ImGui::PopTextWrapPos();
        bool go = false;
        if (modal.input)
        {
            ImGui::TextDisabled("%s", modal.label.c_str());
            ImGui::SetNextItemWidth(420);
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            go = ImGui::InputText("##mi", modal.buf, sizeof modal.buf, ImGuiInputTextFlags_EnterReturnsTrue);
        }
        if (primaryBtn("OK", ImVec2(110, 0)))
            go = true;
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel", ImVec2(110, 0));
        if (go)
        {
            auto f = modal.ok;
            std::string v = modal.buf;
            ImGui::CloseCurrentPopup();
            if (f)
                f(v);
        }
        else if (cancel)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void card(const char *id, float w, const char *label, const std::string &val, const std::string &sub, bool mono = false)
{
    ImGui::BeginChild(id, ImVec2(w, 130), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("%s", label);
    ImGui::SetWindowFontScale(mono ? 1.15f : 1.7f);
    ImGui::TextUnformatted(val.c_str());
    ImGui::SetWindowFontScale(1.f);
    ImGui::TextDisabled("%s", sub.c_str());
    ImGui::EndChild();
}

static std::vector<std::string> hist;
static size_t histIdx = 0;
static int histCb(ImGuiInputTextCallbackData *d)
{
    if (d->EventFlag == ImGuiInputTextFlags_CallbackHistory)
    {
        if (d->EventKey == ImGuiKey_UpArrow && histIdx > 0)
            histIdx--;
        else if (d->EventKey == ImGuiKey_DownArrow && histIdx < hist.size())
            histIdx++;
        d->DeleteChars(0, d->BufTextLen);
        if (histIdx < hist.size())
            d->InsertChars(0, hist[histIdx].c_str());
    }
    return 0;
}

static const char *GROUPS[][2] = {
    {"Server", "PING ECHO COMMAND SAVE LOAD FLUSHALL"}, {"Keys & strings", "SET GET KEYS TYPE DEL EXPIRE RENAME"}, {"List", "LGET LLEN LPUSH RPUSH LPOP RPOP LREM LINDEX LSET"}, {"Hash", "HSET HGET HEXISTS HDEL HGETALL HKEYS HVALS HLEN"}, {"Set", "SADD SREM SISMEMBER SMEMBERS SCARD"}, {"Sorted set", "ZADD ZREM ZSCORE ZRANK ZALL"}};
static const unsigned GROUP_COL[] = {0xE6ECE8, 0x3DD6A8, 0x8FA7FF, 0xE8B04A, 0xFF9EC0, 0xC9A2FF};

static char filterBuf[128] = {}, cmdBuf[512] = {};
static std::string selKey;
static bool focusCmd = false, scrollLog = false;

static void runConsole(const std::string &line)
{
    auto a = words(line);
    if (a.empty())
        return;
    hist.push_back(line);
    histIdx = hist.size();
    std::string tag = a[0];
    std::transform(tag.begin(), tag.end(), tag.begin(), ::toupper);
    submit(a, tag);
    scrollLog = true;
}

static void drawUI(ImGuiIO &io)
{
    Overview o;
    Inspect d;
    std::vector<LogEntry> lg;
    std::string save;
    {
        std::lock_guard<std::mutex> lk(mu);
        o = ov;
        d = ins;
        lg = logv;
        save = lastSave;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ---- header ----
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("Redis Clone");
    ImGui::SetWindowFontScale(1.f);
    ImGui::SameLine(0, 18);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float lh = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 6, p.y + lh * 0.5f + 6), 5.f, ImGui::ColorConvertFloat4ToU32(hex(o.online ? C_OK : C_DANGER)));
    ImGui::Dummy(ImVec2(16, lh));
    ImGui::SameLine(0, 4);
    if (o.online)
        ImGui::Text("PONG - %ld ms", o.ping_ms);
    else
        ImGui::TextColored(hex(C_DANGER), "Unreachable");
    ImGui::SameLine(0, 18);
    ImGui::TextDisabled("%s:%s  RESP over TCP", REDIS_HOST.c_str(), REDIS_PORT.c_str());
        float bw = 96, right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    ImGui::SameLine(right - bw * 4 - ImGui::GetStyle().ItemSpacing.x * 3);
    if (ImGui::Button("BENCH", ImVec2(bw, 0))) showBench = !showBench;
    ImGui::SameLine();
    if (ImGui::Button("LOAD", ImVec2(bw, 0)))
        ask("LOAD", "Replace the in-memory data with redisDB.rdb?", false, "", [](const std::string &)
            { submit({"LOAD"}, "LOAD"); });
    ImGui::SameLine();
    if (primaryBtn("SAVE", ImVec2(bw, 0)))
        submit({"SAVE"}, "SAVE");
    ImGui::SameLine();
    if (dangerBtn("FLUSHALL", ImVec2(bw, 0)))
        ask("FLUSHALL", "This deletes every key. Continue?", false, "", [](const std::string &)
            { selKey.clear(); setWantKey(""); submit({"FLUSHALL"}, "FLUSHALL"); });
    if (!o.online)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, hex(C_DANGER));
        ImGui::TextWrapped("Cannot reach the Redis clone at %s:%s (%s). Start the server; this view retries every 3 s.", REDIS_HOST.c_str(), REDIS_PORT.c_str(), o.error.c_str());
        ImGui::PopStyleColor();
    }

    // ---- stat cards ----
    float gap = ImGui::GetStyle().ItemSpacing.x, full = ImGui::GetContentRegionAvail().x, cw = (full - gap * 3) / 4;
    int types = (int)o.counts.size();
    card("c1", cw, "Total keys", o.online ? std::to_string(o.total) : "-", "KEYS");
    ImGui::SameLine();
    card("c2", cw, "Round trip", o.online ? std::to_string(o.ping_ms) + " ms" : "-", "PING");
    ImGui::SameLine();
    card("c3", cw, "Data types", o.online ? std::to_string(types) : "-", "of 5 in use");
    ImGui::SameLine();
    card("c4", cw, "Snapshot", "redisDB.rdb", save, true);

    // ---- types + persistence ----
    if (ImGui::BeginTable("r2", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch, 2.f); // types (wider)
        ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch, 1.f); // persistence

        ImGui::TableNextColumn();
        ImGui::BeginChild("types", ImVec2(-FLT_MIN, 260), ImGuiChildFlags_Borders); // height 190 -> 260
        ImGui::TextUnformatted("Keys by type");
        ImGui::SameLine();
        ImGui::TextDisabled("KEYS + TYPE");
        int mx = 1;
        for (auto &kv : o.counts)
            mx = std::max(mx, kv.second);
        for (const char *t : {"string", "hash", "list", "set", "sorted_set"})
        {
            int n = o.counts.count(t) ? o.counts.at(t) : 0;
            ImGui::AlignTextToFramePadding(); // new
            ImGui::TextColored(hex(typeColor(t)), "%s", t);
            ImGui::SameLine(140); // 80 -> 140
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hex(typeColor(t)));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, hex(0x1E2823));
            char ov_[16];
            snprintf(ov_, sizeof ov_, "%d", n);
            ImGui::ProgressBar((float)n / mx, ImVec2(-FLT_MIN, 22), ov_); // 14 -> 22
            ImGui::PopStyleColor(2);
        }
        ImGui::EndChild();

        ImGui::TableNextColumn();
        ImGui::BeginChild("persist", ImVec2(-FLT_MIN, 260), ImGuiChildFlags_Borders); // height 190 -> 260
        ImGui::TextUnformatted("Persistence");
        ImGui::TextWrapped("Snapshots are manual. SAVE writes the whole database to redisDB.rdb; LOAD replaces memory with that file.");
        ImGui::TextDisabled("%s", save.c_str());
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
        if (primaryBtn("Save now"))
            submit({"SAVE"}, "SAVE");
        ImGui::SameLine();
        if (ImGui::Button("Load file"))
            ask("LOAD", "Replace the in-memory data with redisDB.rdb?", false, "", [](const std::string &)
                { submit({"LOAD"}, "LOAD"); });
        ImGui::EndChild();
        ImGui::EndTable();
    }

    // ---- key browser + inspector ----
    if (ImGui::BeginTable("r3", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        ImGui::BeginChild("keys", ImVec2(-FLT_MIN, 300), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Key browser");
        ImGui::SameLine(0, 16);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##flt", "Filter keys", filterBuf, sizeof filterBuf);
        std::string f = filterBuf;
        std::transform(f.begin(), f.end(), f.begin(), ::tolower);
        if (ImGui::BeginTable("kt", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(0, o.truncated ? -24 : 0)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 3.f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.f);
            ImGui::TableSetupColumn("Items", ImGuiTableColumnFlags_WidthStretch, 1.f);
            ImGui::TableHeadersRow();
            for (auto &k : o.keys)
            {
                std::string lk = k.key;
                std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
                if (!f.empty() && lk.find(f) == std::string::npos)
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable((k.key + "##k").c_str(), selKey == k.key, ImGuiSelectableFlags_SpanAllColumns))
                {
                    selKey = k.key;
                    setWantKey(selKey);
                }
                ImGui::TableNextColumn();
                ImGui::TextColored(hex(typeColor(k.type)), "%s", k.type.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%lld", k.items);
            }
            ImGui::EndTable();
        }
        if (o.truncated)
            ImGui::TextDisabled("Showing first %zu of %zu keys.", o.keys.size(), o.total);
        ImGui::EndChild();

        ImGui::TableNextColumn();
        ImGui::BeginChild("insp", ImVec2(-FLT_MIN, 300), ImGuiChildFlags_Borders);
        if (selKey.empty())
            ImGui::TextDisabled("Select a key to inspect it");
        else
        {
            ImGui::TextUnformatted(selKey.c_str());
            if (d.key == selKey)
            {
                ImGui::SameLine();
                ImGui::TextColored(hex(typeColor(d.type)), "  %s", d.type.c_str());
            }
            float h = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight() - 12;
            ImGui::BeginChild("ibody", ImVec2(0, h), ImGuiChildFlags_None);
            if (d.key == selKey)
            {
                if (d.type == "hash")
                {
                    if (ImGui::BeginTable("ht", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                    {
                        for (auto &pr : d.pairs)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextDisabled("%s", pr.first.c_str());
                            ImGui::TableNextColumn();
                            ImGui::TextWrapped("%s", pr.second.c_str());
                        }
                        ImGui::EndTable();
                    }
                }
                else if (d.type == "string")
                {
                    ImGui::TextWrapped("%s", d.data.empty() ? "(empty)" : d.data[0].c_str());
                }
                else
                {
                    for (size_t i = 0; i < d.data.size(); i++)
                    {
                        ImGui::TextDisabled("%zu", i);
                        ImGui::SameLine(48);
                        ImGui::TextWrapped("%s", d.data[i].c_str());
                    }
                }
                if (d.pairs.empty() && d.data.empty() && d.type != "string")
                    ImGui::TextDisabled("Empty");
            }
            else
                ImGui::TextDisabled("Loading...");
            ImGui::EndChild();
            // actions
            std::string key = selKey, type = d.key == selKey ? d.type : "";
            const char *addCmd = type == "string" ? "SET" : type == "list" ? "RPUSH"
                                                        : type == "hash"   ? "HSET"
                                                        : type == "set"    ? "SADD"
                                                        : type == "sorted_set"   ? "ZADD"
                                                                           : nullptr;
            if (addCmd)
            {
                if (ImGui::Button(addCmd))
                {
                    const char *hint = type == "string" ? "new value" : type == "list" ? "element to append"
                                                                    : type == "hash"   ? "field value"
                                                                    : type == "set"    ? "member"
                                                                                       : "score member";
                    ask(std::string(addCmd) + " " + key, "", true, hint, [=](const std::string &v)
                        {
                        std::vector<std::string> a{addCmd, key};
                        if (type == "string" || type == "list" || type == "set") a.push_back(v); else for (auto &w : words(v)) a.push_back(w);
                        submit(a); });
                }
                ImGui::SameLine();
            }
            if (ImGui::Button("EXPIRE"))
                ask("EXPIRE " + key, "", true, "seconds", [=](const std::string &v)
                    { submit({"EXPIRE", key, v}); });
            ImGui::SameLine();
            if (ImGui::Button("RENAME"))
                ask("RENAME " + key, "", true, "new key name", [=](const std::string &v)
                    { if (!v.empty()) { submit({"RENAME", key, v}); selKey = v; setWantKey(v); } });
            ImGui::SameLine();
            if (dangerBtn("DEL"))
                ask("DEL " + key, "Delete this key?", false, "", [=](const std::string &)
                    { submit({"DEL", key}); selKey.clear(); setWantKey(""); });
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }

    // ---- console + command reference ----
    float ch = std::max(240.f, ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginTable("r4", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        ImGui::BeginChild("console", ImVec2(-FLT_MIN, ch), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Console");
        ImGui::BeginChild("log", ImVec2(0, -ImGui::GetFrameHeight() - 12), ImGuiChildFlags_Borders);
        for (auto &e : lg)
        {
            ImGui::TextDisabled("> %s", e.cmd.c_str());
            if (e.err)
                ImGui::TextColored(hex(C_DANGER), "%s", e.out.c_str());
            else
                ImGui::TextWrapped("%s", e.out.c_str());
        }
        if (scrollLog || (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4))
        {
            ImGui::SetScrollHereY(1.f);
            scrollLog = false;
        }
        ImGui::EndChild();
        ImGui::SetNextItemWidth(-90);
        if (focusCmd)
        {
            ImGui::SetKeyboardFocusHere();
            focusCmd = false;
        }
        bool enter = ImGui::InputTextWithHint("##cmd", "e.g. SET greeting hello  (Up/Down for history)", cmdBuf, sizeof cmdBuf,
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, histCb);
        ImGui::SameLine();
        bool run = primaryBtn("Run", ImVec2(-FLT_MIN, 0));
        if ((enter || run) && cmdBuf[0])
        {
            runConsole(cmdBuf);
            cmdBuf[0] = 0;
            focusCmd = true;
        }
        ImGui::EndChild();

        ImGui::TableNextColumn();
        ImGui::BeginChild("ref", ImVec2(-FLT_MIN, ch), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Supported commands");
        ImGui::SameLine();
        ImGui::TextDisabled("click to insert");
        float rEdge = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        for (int g = 0; g < 6; g++)
        {
            ImGui::TextColored(hex(GROUP_COL[g]), "%s", GROUPS[g][0]);
            std::string all = GROUPS[g][1];
            size_t st = 0;
            bool first = true;
            while (st < all.size())
            {
                size_t en = all.find(' ', st);
                if (en == std::string::npos)
                    en = all.size();
                std::string c = all.substr(st, en - st);
                st = en + 1;
                float w = ImGui::CalcTextSize(c.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
                if (!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w < rEdge)
                    ImGui::SameLine();
                first = false;
                if (ImGui::SmallButton(c.c_str()))
                {
                    snprintf(cmdBuf, sizeof cmdBuf, "%s ", c.c_str());
                    focusCmd = true;
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }

    drawModal();
    ImGui::End();
    drawBenchWindow();
}

int main(int argc, char **argv)
{
    if (const char *e = getenv("REDIS_HOST"))
        REDIS_HOST = e;
    if (const char *e = getenv("REDIS_PORT"))
        REDIS_PORT = e;
    if (argc > 1)
        REDIS_HOST = argv[1];
    if (argc > 2)
        REDIS_PORT = argv[2];
    signal(SIGPIPE, SIG_IGN);
    if (!glfwInit())
    {
        fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    const char *glsl = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_ALPHA_BITS, 0);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
    GLFWwindow *win = glfwCreateWindow(1600, 1000, "Redis Clone Dashboard", nullptr, nullptr);
    if (!win)
    {
        fprintf(stderr, "cannot create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;

    const float FONT_PX = 20.f;
    if (const char *fp = getenv("DASH_FONT"))
        io.Fonts->AddFontFromFileTTF(fp, FONT_PX);
    else
    {
        ImFontConfig cfg;
        cfg.SizePixels = FONT_PX;
        io.Fonts->AddFontDefault(&cfg);
    }
    applyTheme();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl);

    std::thread net(worker);
    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawUI(io);
        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.055f, 0.075f, 0.067f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }
    {
        std::lock_guard<std::mutex> lk(mu);
        quitting = true;
        cv.notify_all();
    }
    net.join();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
