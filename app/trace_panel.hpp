#pragma once

#include <cstdint>
#include <string>

#include "document.hpp"
#include "task_trace.hpp"

namespace app {

// State of the Trace panel. The runner works on its own thread; the panel keeps a copy of
// the snapshot and refreshes it only when the runner's version changes.
struct TracePanel {
    task::TaskRunner runner;
    task::Services services;  // set by the caller; unset compile/test hooks show as skipped
    task::Snapshot view;
    std::uint64_t seen_version = ~std::uint64_t{0};
};

// Draws the panel contents: Run/Clear, one row per node, and the bounded event log.
// `doc` is the active file (Run is disabled without one); `class_name` comes from the Generate panel.
void draw_trace_panel(TracePanel& panel, const Document* doc, const std::string& class_name);

}  // namespace app
