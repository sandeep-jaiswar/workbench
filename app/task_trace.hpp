#pragma once

// Headless task runner for the Trace panel: a graph of named steps (analyze, generate,
// check, compile, test, report) run on a worker thread. No ImGui, no GLFW, no display.
// The panel reads Snapshot copies; it never touches the graph or the worker directly.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "catalog.hpp"
#include "graph.hpp"

namespace app::task {

class TaskBoard;

enum class NodeStatus { pending, running, ok, failed, skipped };
const char* to_string(NodeStatus status);

// Node names, in the order they appear in the panel.
const std::vector<std::string>& node_names();

// External steps. Set by the caller. An empty compile/test hook records that node as
// skipped. A hook returns a one-line summary, or throws to fail the node.
struct Services {
    // Returns Java source for the pattern named "Singleton" with the given class name.
    std::function<std::string(const std::string& class_name)> generate;
    std::function<std::string()> compile;
    std::function<std::string()> test;
};

// Services that generate with the Singleton pattern from `catalog`. Compile and test are unset.
Services make_services(const wb::Catalog& catalog);

// The graph: analyze -> generate -> check -> compile -> test -> END, with failures routed
// to report -> END. Node rows are written to `board`. The state passed to run() must
// contain "class_name" (string).
wb::graph::Graph build_graph(std::string document_text, Services services,
                             std::shared_ptr<TaskBoard> board);

struct NodeRow {
    std::string name;
    NodeStatus status = NodeStatus::pending;
    double duration_ms = 0.0;
    std::string summary;
};

struct LogLine {
    std::uint64_t seq = 0;
    std::string node;
    std::string kind;  // step_start, step_end, log, error, edge
    std::string message;
};

// Copy of the run state for the UI. Log lines are only filled once the run finished.
struct Snapshot {
    bool running = false;
    bool finished = false;
    std::string outcome;  // "ok", "failed" or "max_steps" once finished
    std::string error;
    std::vector<NodeRow> nodes;
    std::vector<LogLine> log;  // retained events, oldest first
    std::uint64_t dropped = 0;
    std::uint64_t total_recorded = 0;
};

// Mutex-protected state shared by the worker and the UI. `version` changes on every
// row update and on finish, so the UI copies a snapshot only when it has changed.
class TaskBoard {
public:
    void reset();  // all rows pending, log cleared, running = true
    void clear();  // empty snapshot, not running
    void set_row(const std::string& node, NodeStatus status, double duration_ms,
                 const std::string& summary);
    void finish(const wb::graph::Result& result);  // stores log, outcome; running = false
    void fail(const std::string& error);           // run ended without a graph result
    bool running() const;
    std::uint64_t version() const;
    Snapshot snapshot() const;

private:
    mutable std::mutex mu_;
    Snapshot snap_;
    std::uint64_t version_ = 0;
};

// Owns one worker thread at a time.
class TaskRunner {
public:
    TaskRunner();
    ~TaskRunner();  // joins the worker
    TaskRunner(const TaskRunner&) = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

    // Starts a run on a worker thread. Returns false if a run is already active.
    bool start(std::string document_text, std::string class_name, Services services,
               wb::graph::Options options = {});
    // Joins the worker once it has finished. Call once per UI frame; it never blocks on a run.
    void poll();
    // Drops the finished result. Does nothing while a run is active.
    void clear();

    bool running() const { return board_->running(); }
    std::uint64_t version() const { return board_->version(); }
    Snapshot snapshot() const { return board_->snapshot(); }

private:
    std::shared_ptr<TaskBoard> board_;
    std::thread worker_;
};

}  // namespace app::task
