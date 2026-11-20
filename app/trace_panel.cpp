#include "trace_panel.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string_view>

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

// Starts the review for the panel's proposal: loads the diff against the file on disk and
// stages the proposal in a copy of the project on a worker thread.
void open_review(TracePanel& panel, const std::string& project_dir) {
    FixReview& review = panel.review;
    review.proposal = *panel.view.proposal;
    review.open = true;
    review.staged.reset();
    review.message.clear();

    // One diff per edited file, each against the file on disk.
    review.diff.clear();
    for (const fixer::FileEdit& e : review.proposal.edits) {
        std::string current;
        std::ifstream in(std::filesystem::path(project_dir) / e.class_path, std::ios::binary);
        if (in) {
            std::ostringstream buf;
            buf << in.rdbuf();
            current = buf.str();
        } else {
            review.message = "cannot read " + e.class_path + " for the diff";
        }
        review.diff += apply_fix::unified_diff(current, e.new_source, e.class_path);
    }

    review.staging = true;
    const fixer::Proposal proposal = review.proposal;
    review.pending = std::async(std::launch::async, [project_dir, proposal]() {
        return apply_fix::stage(project_dir, proposal);
    });
}

// Moves a finished staging result into the review. Called once per frame.
void poll_review(FixReview& review) {
    if (!review.staging || !review.pending.valid()) return;
    if (review.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    review.staged = review.pending.get();
    review.staging = false;
}

void draw_diff(const std::string& diff) {
    ImGui::BeginChild("fix_diff", ImVec2(0, 170), true, ImGuiWindowFlags_HorizontalScrollbar);
    // Runs every frame while the review is open, so lines are views into `diff`, not copies.
    const std::string_view text(diff);
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        ImVec4 color(0.85f, 0.85f, 0.85f, 1);
        if (line.compare(0, 1, "+") == 0 && line.compare(0, 3, "+++") != 0) {
            color = ImVec4(0.3f, 0.9f, 0.4f, 1);
        } else if (line.compare(0, 1, "-") == 0 && line.compare(0, 3, "---") != 0) {
            color = ImVec4(0.95f, 0.45f, 0.45f, 1);
        } else if (line.compare(0, 2, "@@") == 0) {
            color = ImVec4(0.5f, 0.7f, 1.0f, 1);
        }
        ImGui::TextColored(color, "%.*s", static_cast<int>(line.size()), line.data());
    }
    ImGui::EndChild();
}

// The review: rationale, diff, staged result, and the separate Confirm apply button.
void draw_review(TracePanel& panel, const std::string& project_dir) {
    FixReview& review = panel.review;
    poll_review(review);
    ImGui::Separator();
    std::string files;
    for (const fixer::FileEdit& e : review.proposal.edits) files += (files.empty() ? "" : ", ") + e.class_path;
    ImGui::Text("Proposed fix (%zu file%s): %s", review.proposal.edits.size(),
                review.proposal.edits.size() == 1 ? "" : "s", files.c_str());
    ImGui::TextWrapped("%s", review.proposal.rationale.c_str());
    draw_diff(review.diff);

    if (review.staging) {
        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1), "Staging: compiling and testing a copy...");
    } else if (review.staged) {
        const apply_fix::StageResult& r = *review.staged;
        if (!r.error.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1), "Staging failed: %s", r.error.c_str());
        } else {
            ImGui::TextColored(r.compiled ? ImVec4(0.3f, 0.9f, 0.4f, 1) : ImVec4(0.95f, 0.45f, 0.45f, 1),
                               "Staged compile: %s", r.compiled ? "ok" : "failed");
            ImGui::SameLine();
            if (r.compiled) {
                ImGui::TextColored(r.tested ? ImVec4(0.3f, 0.9f, 0.4f, 1) : ImVec4(0.95f, 0.45f, 0.45f, 1),
                                   "tests: %s", r.tested ? "ok" : "failed");
            } else {
                ImGui::TextDisabled("tests not run");
            }
        }
    }

    const bool can_confirm = review.staged && review.staged->green() && !review.staging &&
                             !panel.view.running;
    ImGui::BeginDisabled(!can_confirm);
    if (ImGui::Button("Confirm apply")) {
        const apply_fix::ApplyResult r =
            apply_fix::apply(project_dir, review.proposal, *review.staged, true);
        if (r.written) {
            review.message = "Applied " + std::to_string(r.written_paths.size()) +
                             " file(s). Backups in out/backups/";
            review.staged.reset();  // a second apply needs a fresh staging run
        } else {
            review.message = r.error;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(review.staging);
    if (ImGui::Button("Close review")) review.open = false;
    ImGui::EndDisabled();
    if (!review.message.empty()) ImGui::TextWrapped("%s", review.message.c_str());
}

}  // namespace

void draw_trace_panel(TracePanel& panel, const Document* doc, const std::string& class_name,
                      const std::string& project_dir) {
    panel.runner.poll();
    bool refreshed = false;
    const std::uint64_t version = panel.runner.version();
    if (version != panel.seen_version) {
        panel.view = panel.runner.snapshot();
        panel.seen_version = version;
        refreshed = true;
    }
    const bool running = panel.view.running;
    // A new run makes the old review stale. A staging still in flight finishes unobserved.
    if (running && panel.review.open) panel.review.open = false;

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
    // Only enabled when the debug node produced a proposal. Opening it stages the proposal.
    const bool has_proposal = panel.view.proposal.has_value();
    ImGui::SameLine();
    ImGui::BeginDisabled(!has_proposal || running || panel.review.staging || project_dir.empty());
    if (ImGui::Button("Apply proposed fix")) open_review(panel, project_dir);
    ImGui::EndDisabled();

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
    if (panel.review.open) draw_review(panel, project_dir);
    ImGui::Separator();
    draw_log(panel.view, refreshed && panel.view.finished);
}

}  // namespace app
