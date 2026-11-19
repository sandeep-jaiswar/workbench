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

#include "architect.hpp"
#include "catalog.hpp"
#include "fixer.hpp"
#include "java_tools.hpp"
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

// The sample runner's exact output (samples/java-demo, test/demo/TestRunner.java).
const char* kSampleTestOutput =
    "PASS demo.CartTest.testAddingSameItemMergesQuantity\n"
    "PASS demo.CartTest.testAddingSameItemWithDifferentPriceIsRejected\n"
    "PASS demo.CartTest.testEmptyCartTotalIsZero\n"
    "PASS demo.CartTest.testRemoveDropsTheLine\n"
    "PASS demo.CartTest.testRemoveUnknownItemThrows\n"
    "PASS demo.CartTest.testSubtotalSumsLineTotals\n"
    "PASS demo.CartTest.testTaxComesFromInjectedPolicy\n"
    "FAIL demo.CartTest.testTaxRoundsHalfUp: AssertionError: expected <$0.11> but was <$0.10>\n"
    "PASS demo.CartTest.testTotalIncludesFlatTax\n"
    "PASS demo.MoneyTest.testMinusBelowZeroIsRejected\n"
    "PASS demo.MoneyTest.testPlusAndMinus\n"
    "PASS demo.MoneyTest.testRejectsNegativeAmounts\n"
    "PASS demo.MoneyTest.testTimesMultipliesByQuantity\n"
    "PASS demo.MoneyTest.testToStringAlwaysShowsTwoDecimals\n"
    "PASS demo.MoneyTest.testZeroIsZero\n"
    "PASS demo.ReceiptTest.testEachItemAppearsWithItsLineTotal\n"
    "PASS demo.ReceiptTest.testEmptyCartRendersZeroTotals\n"
    "PASS demo.ReceiptTest.testSubtotalAndTaxLinesArePresent\n"
    "PASS demo.ReceiptTest.testTotalLineShowsGrandTotal\n"
    "18 passed, 1 failed\n";

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
    CHECK(row(snap, "architect").status == NodeStatus::skipped);
    CHECK(row(snap, "debug").status == NodeStatus::pending);
    CHECK(row(snap, "report").status == NodeStatus::pending);
    CHECK(step_order(snap) == (std::vector<std::string>{"analyze", "architect", "generate", "check", "compile", "test"}));
    CHECK(has_edge(snap, "-> END"));
    CHECK(!has_edge(snap, "-> report"));
    CHECK(!has_edge(snap, "-> debug"));
}

void test_failing_check_routes_to_report() {
    const Snapshot snap = run_sync(kBroken, fake(kBroken));

    CHECK(snap.outcome == "ok");
    CHECK(row(snap, "check").status == NodeStatus::failed);
    CHECK(row(snap, "check").summary.find(" diagnostics") != std::string::npos);
    CHECK(row(snap, "compile").status == NodeStatus::pending);
    CHECK(row(snap, "test").status == NodeStatus::pending);
    CHECK(row(snap, "report").status == NodeStatus::ok);
    CHECK(step_order(snap) == (std::vector<std::string>{"analyze", "architect", "generate", "check", "report"}));
    CHECK(has_edge(snap, "-> report"));
    CHECK(!has_edge(snap, "-> compile"));
}

void test_unwired_hooks_are_skipped() {
    const Snapshot snap = run_sync(kGood, fake(kGood));

    CHECK(snap.outcome == "ok");
    CHECK(row(snap, "compile").status == NodeStatus::skipped);
    CHECK(row(snap, "test").status == NodeStatus::skipped);
    CHECK(row(snap, "report").status == NodeStatus::pending);
    CHECK(step_order(snap) == (std::vector<std::string>{"analyze", "architect", "generate", "check", "compile", "test"}));
}

void test_generate_failure_routes_to_report() {
    Services s;
    s.generate = [](const std::string&) -> std::string { throw std::runtime_error("boom"); };
    const Snapshot snap = run_sync(kGood, s);

    CHECK(row(snap, "generate").status == NodeStatus::failed);
    CHECK(row(snap, "generate").summary == "boom");
    CHECK(row(snap, "check").status == NodeStatus::pending);
    CHECK(row(snap, "report").status == NodeStatus::ok);
    CHECK(step_order(snap) == (std::vector<std::string>{"analyze", "architect", "generate", "report"}));
}

void test_compile_failure_routes_to_report() {
    Services s = fake(kGood);
    s.compile = []() -> std::string { throw std::runtime_error("javac exit 1"); };
    const Snapshot snap = run_sync(kGood, s);

    CHECK(row(snap, "compile").status == NodeStatus::failed);
    CHECK(row(snap, "test").status == NodeStatus::pending);
    CHECK(row(snap, "report").status == NodeStatus::ok);
    CHECK(step_order(snap) == (std::vector<std::string>{"analyze", "architect", "generate", "check", "compile", "report"}));
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

void test_failing_test_routes_to_report() {
    Services s = fake(kGood);
    s.compile = [] { return std::string("1 class compiled"); };
    s.test = []() -> std::string {
        throw std::runtime_error("1 passed, 1 failed; FAIL demo.CartTest.testTaxRoundsHalfUp");
    };
    const Snapshot snap = run_sync(kGood, s);

    CHECK(snap.outcome == "ok");
    CHECK(row(snap, "test").status == NodeStatus::failed);
    CHECK(row(snap, "test").summary.find("demo.CartTest.testTaxRoundsHalfUp") != std::string::npos);
    CHECK(row(snap, "debug").status == NodeStatus::skipped);
    CHECK(row(snap, "debug").summary == "not wired (no fixer)");
    CHECK(row(snap, "report").status == NodeStatus::ok);
    CHECK(row(snap, "report").summary.find("stopped at test: 1 passed, 1 failed") != std::string::npos);
    CHECK(step_order(snap) == (std::vector<std::string>{
              "analyze", "architect", "generate", "check", "compile", "test", "debug", "report"}));
    CHECK(has_edge(snap, "-> debug"));
    CHECK(has_edge(snap, "-> report"));
}

void test_parse_sample_output() {
    const wbx::TestResult t = wbx::parse_test_output(kSampleTestOutput);
    CHECK(t.passed == 18);
    CHECK(t.failed == 1);
    CHECK(t.failing == (std::vector<std::string>{"demo.CartTest.testTaxRoundsHalfUp"}));

    // Without a summary line, counts come from PASS and FAIL lines (CRLF tolerated).
    const wbx::TestResult lines = wbx::parse_test_output("PASS a.B.one\r\nFAIL a.B.two: x\r\n");
    CHECK(lines.passed == 1);
    CHECK(lines.failed == 1);
    CHECK(lines.failing == (std::vector<std::string>{"a.B.two"}));

    // Javac output or nothing at all: no counts, no names.
    const wbx::TestResult none = wbx::parse_test_output("Main.java:3: error: ';' expected\n");
    CHECK(none.passed == 0 && none.failed == 0 && none.failing.empty());
    CHECK(wbx::parse_test_output("").failing.empty());
}

void test_architect_counts() {
    const std::vector<app::architect::Source> sources = {
        {"demo/Money.java",
         "package demo;\n"
         "public final class Money {\n"
         "    private final long cents;\n"
         "    public long cents() { return cents; }\n"
         "    public Money plus(Money o) { return this; }\n"
         "    static class Helper { void run() {} }\n"
         "}\n"},
        {"demo/Shape.java",
         "package demo;\n"
         "interface Shape { double area(); }\n"
         "enum Unit { CM, MM }\n"},
    };
    const app::architect::Outline o = app::architect::outline_sources(sources);
    CHECK(o.files == 2);
    CHECK(o.types == 4);     // Money, Helper (nested), Shape, Unit
    CHECK(o.methods == 4);   // cents, plus, run, area
    CHECK(o.fields == 1);    // cents
    CHECK(!o.truncated);
    CHECK(o.entries.size() == 2);
    CHECK(o.entries[0].package == "demo");
    CHECK(o.entries[0].types.size() == 2);
    CHECK(o.entries[0].types[0].name == "Money");
    CHECK(o.entries[0].types[0].methods == 2);
    CHECK(o.entries[0].types[0].fields == 1);
    CHECK(o.entries[0].types[1].nested);
    CHECK(app::architect::summarize(o) == "2 files, 4 types, 4 methods");

    // A file over the byte cap is skipped and recorded.
    const std::vector<app::architect::Source> big = {
        {"Big.java", std::string(app::architect::kMaxFileBytes + 1, ' ')}};
    const app::architect::Outline skipped = app::architect::outline_sources(big);
    CHECK(skipped.files == 0);
    CHECK(skipped.skipped.size() == 1);

    // Through the graph: the architect row shows the summary line.
    Services s = fake(kGood);
    s.architect = [sources] { return app::architect::outline_sources(sources); };
    s.compile = [] { return std::string("javac exit 0"); };
    s.test = [] { return std::string("1 passed, 0 failed"); };
    const Snapshot snap = run_sync(kGood, s);
    CHECK(row(snap, "architect").status == NodeStatus::ok);
    CHECK(row(snap, "architect").summary == "2 files, 4 types, 4 methods");
}

void test_architect_failure_routes_to_report() {
    Services s = fake(kGood);
    s.architect = []() -> app::architect::Outline { throw std::runtime_error("no src directory"); };
    const Snapshot snap = run_sync(kGood, s);
    CHECK(row(snap, "architect").status == NodeStatus::failed);
    CHECK(row(snap, "generate").status == NodeStatus::pending);
    CHECK(row(snap, "report").status == NodeStatus::ok);
    CHECK(step_order(snap) == (std::vector<std::string>{"analyze", "architect", "report"}));
}

// The fake fixer returns a fixed proposal. Debug records the rationale, keeps the proposal on
// the board, and routes to report: nothing is applied and test is not run again.
void test_fake_fixer_records_rationale_and_routes_to_report() {
    int test_runs = 0;
    int fixer_calls = 0;
    app::fixer::FailureContext seen;
    Services s = fake(kGood);
    s.compile = [] { return std::string("javac exit 0"); };
    s.test = [&test_runs]() -> std::string {
        ++test_runs;
        throw std::runtime_error("0 passed, 1 failed; FAIL demo.CartTest.testTaxRoundsHalfUp");
    };
    s.fixer = [&](const app::fixer::FailureContext& ctx) {
        ++fixer_calls;
        seen = ctx;
        app::fixer::Proposal p;
        p.class_path = "src/demo/Demo.java";
        p.new_source = "public class Demo {}\n";
        p.rationale = "round half up\nin FlatTax";
        return p;
    };
    const Snapshot snap = run_sync(kGood, s);

    CHECK(fixer_calls == 1);
    CHECK(test_runs == 1);
    CHECK(seen.test_output.find("testTaxRoundsHalfUp") != std::string::npos);
    CHECK(seen.class_source == kGood);
    CHECK(seen.attempt == 1);
    CHECK(snap.outcome == "ok");
    CHECK(row(snap, "debug").status == NodeStatus::ok);
    CHECK(row(snap, "debug").summary == "attempt 1: round half up in FlatTax");
    CHECK(row(snap, "report").status == NodeStatus::ok);
    CHECK(row(snap, "report").summary.find("fix attempts 1") != std::string::npos);
    CHECK(step_order(snap) == (std::vector<std::string>{
              "analyze", "architect", "generate", "check", "compile", "test", "debug", "report"}));
    CHECK(has_edge(snap, "-> report"));
    CHECK(has_edge(snap, "-> debug"));
    CHECK(snap.proposal.has_value());
    CHECK(snap.proposal->class_path == "src/demo/Demo.java");
    CHECK(snap.proposal->base_source == kGood);
}

// Without a fixer the debug step is skipped and no proposal is stored.
void test_unset_fixer_records_skipped_without_proposal() {
    Services s = fake(kGood);
    s.compile = [] { return std::string("javac exit 0"); };
    s.test = []() -> std::string { throw std::runtime_error("0 passed, 1 failed"); };
    const Snapshot snap = run_sync(kGood, s);

    CHECK(row(snap, "debug").status == NodeStatus::skipped);
    CHECK(row(snap, "debug").summary == "not wired (no fixer)");
    CHECK(!snap.proposal.has_value());
    CHECK(row(snap, "report").status == NodeStatus::ok);
}

void test_fixer_that_throws_routes_to_report() {
    Services s = fake(kGood);
    s.compile = [] { return std::string("javac exit 0"); };
    s.test = []() -> std::string { throw std::runtime_error("0 passed, 1 failed"); };
    s.fixer = [](const app::fixer::FailureContext&) -> app::fixer::Proposal {
        throw std::runtime_error("no fix");
    };
    const Snapshot snap = run_sync(kGood, s);

    CHECK(row(snap, "debug").status == NodeStatus::skipped);
    CHECK(row(snap, "debug").summary == "no proposal: no fix");
    CHECK(!snap.proposal.has_value());
    CHECK(row(snap, "test").status == NodeStatus::failed);
    CHECK(row(snap, "report").status == NodeStatus::ok);
    CHECK(step_order(snap) == (std::vector<std::string>{
              "analyze", "architect", "generate", "check", "compile", "test", "debug", "report"}));
}

void test_panel_rows_match_graph_nodes() {
    // Every node the graph runs has a row, so the panel table follows the graph.
    Services s = fake(kGood);
    s.compile = [] { return std::string("javac exit 0"); };
    s.test = []() -> std::string { throw std::runtime_error("0 passed, 1 failed"); };
    s.fixer = [](const app::fixer::FailureContext&) {
        app::fixer::Proposal p;
        p.class_path = "src/demo/Demo.java";
        p.rationale = "no-op";
        return p;
    };
    const Snapshot snap = run_sync(kGood, s);
    for (const std::string& name : step_order(snap)) {
        bool found = false;
        for (const auto& r : snap.nodes) found = found || r.name == name;
        CHECK(found);
    }
    CHECK(snap.nodes.size() == app::task::node_names().size());
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
    test_failing_test_routes_to_report();
    test_parse_sample_output();
    test_architect_counts();
    test_architect_failure_routes_to_report();
    test_fake_fixer_records_rationale_and_routes_to_report();
    test_unset_fixer_records_skipped_without_proposal();
    test_fixer_that_throws_routes_to_report();
    test_panel_rows_match_graph_nodes();
    if (argc > 1) test_real_singleton(argv[1]);

    if (failures == 0) std::printf("trace_panel_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
