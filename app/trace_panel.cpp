#include "trace_panel.hpp"

#include <cstdio>

#include "imgui.h"

namespace app {
namespace {

ImVec4 status_color(task::NodeStatus status) {
    switch (status) {
        case task::NodeStatus::ok: return ImVec4(0.3f, 0.9f, 0.4f, 1);
        case task::NodeStatus::failed: return ImVec4(0.95f, 0.45f, 0.45f, 1);
        case task::NodeStatus::running: return ImVec4(0.95f, 0.8f, 0.3f, 1);
        case task::NodeStatus::skipped: return ImVec4(0.6f, 0.6f, 0.6f, 1);
        case task::NodeStatus::pending: break;
    }
    return ImVec4(0.75f, 0.75f, 0.75f, 1);
}

void draw_nodes(const task::Snapshot& view) {
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("trace_nodes", 4, flags)) return;
    ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Summary");
    ImGui::TableHeadersRow();
    for (const auto& row : view.nodes) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(row.name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(status_color(row.status), "%s", task::to_string(row.status));
        ImGui::TableSetColumnIndex(2);
        if (row.status != task::NodeStatus::pending && row.status != task::NodeStatus::running) {
            ImGui::Text("%.2f", row.duration_ms);
        }
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(row.summary.c_str());
    }
    ImGui::EndTable();
}

void draw_log(const task::Snapshot& view, bool scroll_to_end) {
    ImGui::TextUnformatted("Event log");
    if (view.dropped > 0) {
        ImGui::TextDisabled("%llu older events dropped (log keeps the newest %zu)",
                            static_cast<unsigned long long>(view.dropped), view.log.size());
    }
    ImGui::BeginChild("trace_log", ImVec2(0, 0), true);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(view.log.size()));
    char line[600];
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const task::LogLine& e = view.log[static_cast<std::size_t>(i)];
            std::snprintf(line, sizeof(line), "#%llu  %-8s %-10s %s",
                          static_cast<unsigned long long>(e.seq), e.node.c_str(), e.kind.c_str(),
                          e.message.c_str());
            ImGui::TextUnformatted(line);
        }
    }
    if (scroll_to_end) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

}  // namespace

void draw_trace_panel(TracePanel& panel, const Document* doc, const std::string& class_name) {
    panel.runner.poll();
    bool refreshed = false;
    const std::uint64_t version = panel.runner.version();
    if (version != panel.seen_version) {
        panel.view = panel.runner.snapshot();
        panel.seen_version = version;
        refreshed = true;
    }
    const bool running = panel.view.running;

    ImGui::BeginDisabled(running || doc == nullptr);
    if (ImGui::Button("Run task") && doc != nullptr) {
        panel.runner.start(doc->text(), class_name, panel.services);
    }
    ImGui::EndDisabled();
    if (doc == nullptr) {
        ImGui::SameLine();
        ImGui::TextDisabled("Open a .java file first.");
    }
    if (!running && panel.view.finished) {
        ImGui::SameLine();
        if (ImGui::Button("Clear")) panel.runner.clear();
    }

    if (running) {
        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1), "Running...");
    } else if (panel.view.finished) {
        ImGui::Text("Finished: %s", panel.view.outcome.c_str());
        if (!panel.view.error.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1), "%s", panel.view.error.c_str());
        }
    } else {
        ImGui::TextDisabled("Idle");
    }
    ImGui::Separator();

    draw_nodes(panel.view);
    ImGui::Separator();
    draw_log(panel.view, refreshed && panel.view.finished);
}

}  // namespace app
