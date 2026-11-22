// bench_panel.h - ImGui "Benchmark" window. Include AFTER hex(), C_OK, C_DANGER, primaryBtn(),
// dangerBtn(), REDIS_HOST and REDIS_PORT are defined, and BEFORE drawUI().
#pragma once
#include "bench.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

static bool showBench = false;

namespace benchui {
struct Row { uint64_t id; std::string wl; int threads; bool reconnect; uint64_t ops, errors; double ops_s, avg, p50, p99; };
inline std::string us(double v) {
    char b[32];
    if (v >= 1e6) snprintf(b, sizeof b, "%.2f s", v / 1e6);
    else if (v >= 1e3) snprintf(b, sizeof b, "%.2f ms", v / 1e3);
    else snprintf(b, sizeof b, "%.0f us", v);
    return b;
}
inline void stat(const char *label, const std::string &val) {
    ImGui::TextDisabled("%s", label);
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted(val.c_str());
    ImGui::SetWindowFontScale(1.f);
}
} // namespace benchui

static void drawBenchWindow() {
    if (!showBench) return;
    static bench::Config cfg;
    static std::vector<benchui::Row> history;
    static uint64_t recorded = 0;

    ImGui::SetNextWindowSize(ImVec2(960, 860), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Benchmark", &showBench)) { ImGui::End(); return; }

    bench::Result r = bench::runner().snapshot();
    if (r.done && r.run_id != recorded) { // log each finished run once
        recorded = r.run_id;
        if (r.ok > 0) history.push_back({r.run_id, r.workload, r.threads, r.reconnect, r.ok, r.errors, r.ops_per_sec, r.avg_us, r.p50_us, r.p99_us});
    }

    // ---- configuration ----
    ImGui::BeginDisabled(r.running);
    ImGui::SetNextItemWidth(320);
    ImGui::Combo("Workload", &cfg.workload, bench::WORKLOAD_NAMES, bench::W_COUNT);
    ImGui::SetNextItemWidth(320);
    ImGui::InputInt("Total operations", &cfg.total_ops, 1000, 10000);
    ImGui::SameLine();
    if (ImGui::SmallButton("5k")) cfg.total_ops = 5000;
    ImGui::SameLine();
    if (ImGui::SmallButton("50k")) cfg.total_ops = 50000;
    ImGui::SameLine();
    if (ImGui::SmallButton("500k")) cfg.total_ops = 500000;
    ImGui::SetNextItemWidth(320);
    ImGui::InputInt("Connections (threads)", &cfg.threads);
    ImGui::SetNextItemWidth(320);
    ImGui::InputInt("Payload bytes", &cfg.payload, 8, 64);
    ImGui::SetNextItemWidth(320);
    ImGui::InputInt("Key space", &cfg.keyspace, 100, 1000);
    ImGui::Checkbox("New TCP connection per operation (includes connect cost)", &cfg.reconnect);
    ImGui::Checkbox("Delete bench:* keys afterwards", &cfg.cleanup);
    ImGui::EndDisabled();
    cfg.total_ops = std::clamp(cfg.total_ops, 1, 5000000);
    cfg.threads = std::clamp(cfg.threads, 1, 256);
    cfg.payload = std::clamp(cfg.payload, 0, 1 << 20);
    cfg.keyspace = std::clamp(cfg.keyspace, 1, 100000);

    if (!r.running) {
        if (primaryBtn("Run benchmark", ImVec2(200, 0))) {
            cfg.host = REDIS_HOST;
            cfg.port = REDIS_PORT;
            bench::runner().start(cfg);
        }
    } else if (dangerBtn("Stop", ImVec2(200, 0))) bench::runner().stop();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", r.status.c_str());
    ImGui::ProgressBar(r.total ? std::min(1.f, (float)r.completed / (float)r.total) : 0.f, ImVec2(-FLT_MIN, 0));

    // ---- results ----
    if (r.ok > 0 || r.errors > 0) {
        ImGui::Separator();
        if (ImGui::BeginTable("big", 4, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn(); benchui::stat("Throughput", r.done ? std::to_string((long long)r.ops_per_sec) + " ops/s" : "...");
            ImGui::TableNextColumn(); benchui::stat("Median (p50)", benchui::us(r.p50_us));
            ImGui::TableNextColumn(); benchui::stat("p99", benchui::us(r.p99_us));
            ImGui::TableNextColumn(); benchui::stat("Errors", std::to_string(r.errors));
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("pct", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
            const char *h[] = {"min", "avg", "p50", "p90", "p95", "p99", "p99.9", "max"};
            const double v[] = {r.min_us, r.avg_us, r.p50_us, r.p90_us, r.p95_us, r.p99_us, r.p999_us, r.max_us};
            for (const char *s : h) ImGui::TableSetupColumn(s);
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            for (double x : v) { ImGui::TableNextColumn(); ImGui::TextUnformatted(benchui::us(x).c_str()); }
            ImGui::EndTable();
        }
        if (r.done) ImGui::TextDisabled("%llu ok in %.2f s, %d connection(s)%s", (unsigned long long)r.ok, r.seconds, r.threads, r.reconnect ? ", new connection per op" : "");
        if (r.unframed) {
            ImGui::PushStyleColor(ImGuiCol_Text, hex(C_DANGER));
            ImGui::TextWrapped("%llu replies were not CRLF-terminated. Each one waited 150 ms for more data, so latencies are inflated. Fix the server's reply framing for accurate numbers.", (unsigned long long)r.unframed);
            ImGui::PopStyleColor();
        }
        if (!r.series.empty())
            ImGui::PlotLines("##tp", r.series.data(), (int)r.series.size(), 0, "throughput (ops/s, 100 ms samples)", 0.f, FLT_MAX, ImVec2(-FLT_MIN, 100));
        if (r.done) {
            float hb[20];
            for (int i = 0; i < 20; i++) hb[i] = (float)r.hist[(size_t)i];
            ImGui::PlotHistogram("##hist", hb, 20, 0, "latency distribution (log2 buckets, 1 us ... 524 ms+)", 0.f, FLT_MAX, ImVec2(-FLT_MIN, 100));
        }
    }

    // ---- run history (compare before/after changes) ----
    if (!history.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Run history");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy CSV")) {
            std::string csv = "run,workload,connections,new_conn_per_op,ops,errors,ops_per_sec,avg_us,p50_us,p99_us\n";
            char b[256];
            for (auto &h : history) {
                snprintf(b, sizeof b, "%llu,%s,%d,%d,%llu,%llu,%.0f,%.0f,%.0f,%.0f\n", (unsigned long long)h.id, h.wl.c_str(), h.threads, (int)h.reconnect,
                         (unsigned long long)h.ops, (unsigned long long)h.errors, h.ops_s, h.avg, h.p50, h.p99);
                csv += b;
            }
            ImGui::SetClipboardText(csv.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) history.clear();
        if (ImGui::BeginTable("hist", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            for (const char *s : {"#", "Workload", "Conns", "Ops", "Ops/s", "avg", "p50", "p99"}) ImGui::TableSetupColumn(s);
            ImGui::TableHeadersRow();
            for (auto it = history.rbegin(); it != history.rend(); ++it) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)it->id);
                ImGui::TableNextColumn(); ImGui::Text("%s%s", it->wl.c_str(), it->reconnect ? " (reconnect)" : "");
                ImGui::TableNextColumn(); ImGui::Text("%d", it->threads);
                ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)it->ops);
                ImGui::TableNextColumn(); ImGui::Text("%.0f", it->ops_s);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(benchui::us(it->avg).c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(benchui::us(it->p50).c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(benchui::us(it->p99).c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}
