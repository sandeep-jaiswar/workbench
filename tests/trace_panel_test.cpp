// Headless tests for the Trace panel's task runner. Links wb_core only: no ImGui, GLFW or display.
// Usage: trace_panel_test [catalog.json]. With a catalog, one case also runs the real Singleton generator.
// CHECK is used instead of assert because Release builds define NDEBUG.
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "catalog.hpp"
#include "task_trace.hpp"

namespace {

using app::task::NodeRow;
using app::task::NodeStatus;
using app::task::Services;
using app::task::Snapshot;
using app::task::TaskBoard;

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                             \
            ++failures;                                                \
        }                                                              \
    } while (0)

const char* kGood =
    "public class Demo {\n"
    "    private static Demo instance;\n"
    "    public static Demo getInstance() { return instance; }\n"
    "}\n";

const char* kBroken = "public class Demo {\n    void f( {\n}\n";

Services fake(const std::string& source) {
    Services s;
    s.generate = [source](const std::string&) { return source; };
    return s;
}

Snapshot run_sync(const std::string& doc, Services services,
                  const wb::graph::Options& options = {}) {
    auto board = std::make_shared<TaskBoard>();
    board->reset();
    auto graph = app::task::build_graph(doc, std::move(services), board);
    wb::graph::State state;
    state.set("class_name", std::string("Demo"));
    board->finish(graph.run(state, options));
    return board->snapshot();
}

const NodeRow& row(const Snapshot& s, const std::string& name) {
    static const NodeRow missing;
    for (const auto& r : s.nodes) {
        if (r.name == name) return r;
    }
    std::fprintf(stderr, "missing row %s\n", name.c_str());
    return missing;
}

// Node names of the step_start events, in order.
std::vector<std::string> step_order(const Snapshot& s) {
    std::vector<std::string> out;
    for (const auto& e : s.log) {
        if (e.kind == "step_start") out.push_back(e.node);
    }
    return out;
}

bool has_edge(const Snapshot& s, const std::string& edge) {
    for (const auto& e : s.log) {
        if (e.kind == "edge" && e.message == edge) return true;
    }
    return false;
}

void test_happy_path() {
    Services s = fake(kGood);
    s.compile = [] { return std::string("1 class compiled"); };
    s.test = [] { return std::string("3 tests passed"); };
    const Snapshot snap = run_sync(kGood, s);

    CHECK(snap.outcome == "ok");
    CHECK(row(snap, "analyze").status == NodeStatus::ok);
    CHECK(row(snap, "generate").status == NodeStatus::ok);
    CHECK(row(snap, "check").status == NodeStatus::ok);
    CHECK(row(snap, "check").summary == "0 diagnostics");
    CHECK(row(snap, "compile").status == NodeStatus::ok);
    CHECK(row(snap, "compile").summary == "1 class compiled");
    CHECK(row(snap, "test").status == NodeStatus::ok);
    CHECK(row(snap, "report").status == NodeStatus::pending);
    CHECK(step_order(snap) == (std::vector<std::string>{"analyze", "generate", "check", "compile", "test"}));
    CHECK(has_edge(snap, "-> END"));
    CHECK(!has_edge(snap, "-> report"));
}

void test_failing_check_routes_to_report() {
    const Snapshot snap = run_sync(kBroken, fake(kBroken));

    CHECK(snap.outcome == "ok");
    CHECK(row(snap, "check").status == NodeStatus::failed);
    CHECK(row(snap, "check").summary.find(" diagnostics") != std::string::npos);
    CHECK(row(snap, "compile").status == NodeStatus::pending);
    CHECK(row(snap, "test").status == NodeStatus::pending);
    CHECK(row(snap, "report").status == NodeStatus::ok);
    CHECK(step_order(snap) == (std::vector<std::string>{"analyze", "generate", "check", "report"}));
    CHECK(has_edge(snap, "-> report"));
    CHECK(!has_edge(snap, "-> compile"));
}

void test_unwired_hooks_are_skipped() {
    const Snapshot snap = run_sync(kGood, fake(kGood));

    CHECK(snap.outcome == "ok");
    CHECK(row(snap, "compile").status == NodeStatus::skipped);
    CHECK(row(snap, "test").status == NodeStatus::skipped);
    CHECK(row(snap, "report").status == NodeStatus::pending);
    CHECK(step_order(snap) == (std::vector<std::string>{"analyze", "generate", "check", "compile", "test"}));
}

void test_generate_failure_routes_to_report() {
    Services s;
    s.generate = [](const std::string&) -> std::string { throw std::runtime_error("boom"); };
    const Snapshot snap = run_sync(kGood, s);

    CHECK(row(snap, "generate").status == NodeStatus::failed);
    CHECK(row(snap, "generate").summary == "boom");
    CHECK(row(snap, "check").status == NodeStatus::pending);
    CHECK(row(snap, "report").status == NodeStatus::ok);
    CHECK(step_order(snap) == (std::vector<std::string>{"analyze", "generate", "report"}));
}

void test_compile_failure_routes_to_report() {
    Services s = fake(kGood);
    s.compile = []() -> std::string { throw std::runtime_error("javac exit 1"); };
    const Snapshot snap = run_sync(kGood, s);

    CHECK(row(snap, "compile").status == NodeStatus::failed);
    CHECK(row(snap, "test").status == NodeStatus::pending);
    CHECK(row(snap, "report").status == NodeStatus::ok);
    CHECK(step_order(snap) == (std::vector<std::string>{"analyze", "generate", "check", "compile", "report"}));
}

void test_log_is_bounded() {
    wb::graph::Options options;
    options.trace_capacity = 4;
    const Snapshot snap = run_sync(kGood, fake(kGood), options);

    CHECK(snap.log.size() == 4);
    CHECK(snap.dropped > 0);
    CHECK(snap.total_recorded == snap.dropped + snap.log.size());
    // Retained events stay in order and end with the last edge.
    for (std::size_t i = 1; i < snap.log.size(); ++i) {
        CHECK(snap.log[i - 1].seq < snap.log[i].seq);
    }
}

void test_runner_thread() {
    app::task::TaskRunner runner;
    CHECK(runner.start(kGood, "Demo", fake(kGood)));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (runner.running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    runner.poll();
    const Snapshot snap = runner.snapshot();
    CHECK(!snap.running);
    CHECK(snap.finished);
    CHECK(snap.outcome == "ok");
    CHECK(row(snap, "check").status == NodeStatus::ok);
    CHECK(!snap.log.empty());

    runner.clear();
    CHECK(!runner.snapshot().finished);
}

void test_real_singleton(const std::string& catalog_path) {
    const wb::Catalog catalog = wb::Catalog::load(catalog_path);
    const Snapshot snap = run_sync("", app::task::make_services(catalog));

    CHECK(row(snap, "generate").status == NodeStatus::ok);
    CHECK(row(snap, "check").status == NodeStatus::ok);
    CHECK(row(snap, "compile").status == NodeStatus::skipped);
}

}  // namespace

int main(int argc, char** argv) {
    test_happy_path();
    test_failing_check_routes_to_report();
    test_unwired_hooks_are_skipped();
    test_generate_failure_routes_to_report();
    test_compile_failure_routes_to_report();
    test_log_is_bounded();
    test_runner_thread();
    if (argc > 1) test_real_singleton(argv[1]);

    if (failures == 0) std::printf("trace_panel_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
