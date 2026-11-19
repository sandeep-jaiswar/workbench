#pragma once

#include <cstdint>
#include <future>
#include <optional>
#include <string>

#include "apply_fix.hpp"
#include "document.hpp"
#include "task_trace.hpp"

namespace app {

// The apply review for one proposal: diff, staged result, and the confirm step. Staging runs
// on a worker thread; Confirm stays disabled until the staged build and tests are green.
struct FixReview {
    bool open = false;
    fixer::Proposal proposal;  // the proposal that was staged and is shown
    std::string diff;
    std::future<apply_fix::StageResult> pending;
    bool staging = false;
    std::optional<apply_fix::StageResult> staged;
    std::string message;  // result of the last apply, or why it was refused
};

// State of the Trace panel. The runner works on its own thread; the panel keeps a copy of
// the snapshot and refreshes it only when the runner's version changes.
struct TracePanel {
    task::TaskRunner runner;
    task::Services services;  // set by the caller; unset compile/test hooks show as skipped
    task::Snapshot view;
    std::uint64_t seen_version = ~std::uint64_t{0};
    FixReview review;
};

// Draws the panel contents: Run/Clear, one row per node, the fix review, and the bounded event
// log. `doc` is the active file (Run is disabled without one); `class_name` comes from the
// Generate panel; `project_dir` is the open project (stage and apply need it).
void draw_trace_panel(TracePanel& panel, const Document* doc, const std::string& class_name,
                      const std::string& project_dir);

}  // namespace app
