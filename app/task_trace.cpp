#include "task_trace.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <stdexcept>
#include <utility>

#include "engine.hpp"
#include "session.hpp"
#include "verify.hpp"

namespace app::task {
namespace {

using wb::graph::Graph;
using wb::graph::NodeFn;
using wb::graph::Patch;
using wb::graph::State;
using wb::graph::Trace;

struct Outcome {
    NodeStatus status = NodeStatus::ok;
    std::string summary;
};

// A step does its work, may write keys into the patch, and returns what to show on its row.
// Throwing is allowed: the wrapper turns it into a failed row, so the graph still routes.
using StepFn = std::function<Outcome(const State&, Trace&, Patch&)>;

std::size_t count_lines(const std::string& s) {
    std::size_t n = 1;
    for (char c : s) n += c == '\n';
    return n;
}

bool flag(const State& s, const char* key) {
    const auto* v = s.get(key);
    return v != nullptr && v->is_boolean() && v->get<bool>();
}

int integer(const State& s, const char* key) {
    const auto* v = s.get(key);
    return v != nullptr && v->is_number_integer() ? v->get<int>() : 0;
}

// Per-file, per-type rows for the architect step. Counts only, no symbol lists.
nlohmann::json outline_json(const app::architect::Outline& o) {
    nlohmann::json files = nlohmann::json::array();
    for (const auto& f : o.entries) {
        nlohmann::json types = nlohmann::json::array();
        for (const auto& t : f.types) {
            types.push_back({{"name", t.name}, {"kind", t.kind}, {"nested", t.nested},
                             {"methods", t.methods}, {"fields", t.fields}});
        }
        files.push_back({{"file", f.path}, {"package", f.package}, {"types", types}});
    }
    return files;
}

std::string text(const State& s, const char* key) {
    const auto* v = s.get(key);
    return v != nullptr && v->is_string() ? v->get<std::string>() : std::string();
}

// Wraps a step: updates the board before and after, records duration, and sets "<node>_ok"
// (true unless the step failed or threw). Skipped counts as ok so the run continues.
NodeFn guarded(std::string name, StepFn step, std::shared_ptr<TaskBoard> board) {
    return [name = std::move(name), step = std::move(step), board](const State& s, Trace& t) {
        board->set_row(name, NodeStatus::running, 0.0, "");
        const std::int64_t started = wb::graph::monotonic_ns();
        Patch patch;
        Outcome out;
        try {
            out = step(s, t, patch);
        } catch (const std::exception& e) {
            out = Outcome{NodeStatus::failed, e.what()};
        } catch (...) {
            out = Outcome{NodeStatus::failed, "unknown exception"};
        }
        const double ms = static_cast<double>(wb::graph::monotonic_ns() - started) / 1e6;
        if (out.status == NodeStatus::failed) {
            patch["reason"] = name + ": " + out.summary;
            t.log(out.summary);
        }
        patch[name + "_ok"] = out.status != NodeStatus::failed;
        board->set_row(name, out.status, ms, out.summary);
        return patch;
    };
}

// Keeps the event log readable when a project has many unreadable files.
constexpr std::size_t kMaxLoggedSkips = 20;

Outcome run_hook(const std::function<std::string()>& hook) {
    if (!hook) return Outcome{NodeStatus::skipped, "not wired (java_tools not connected)"};
    return Outcome{NodeStatus::ok, hook()};
}

}  // namespace

const char* to_string(NodeStatus status) {
    switch (status) {
        case NodeStatus::pending: return "pending";
        case NodeStatus::running: return "running";
        case NodeStatus::ok: return "ok";
        case NodeStatus::failed: return "failed";
        case NodeStatus::skipped: return "skipped";
    }
    return "pending";
}

const std::vector<std::string>& node_names() {
    static const std::vector<std::string> names = {"analyze", "architect", "generate", "check",
                                                   "compile", "test",     "debug",    "report"};
    return names;
}

Services make_services(const wb::Catalog& catalog) {
    std::optional<wb::Pattern> singleton;
    for (const auto& p : catalog.patterns()) {
        if (p.name == "Singleton") {
            singleton = p;
            break;
        }
    }
    Services s;
    s.generate = [singleton](const std::string& class_name) -> std::string {
        if (!singleton) throw std::runtime_error("catalog has no Singleton pattern");
        wb::Verifier verifier;
        const wb::GenerateResult r = wb::generate(verifier, *singleton, {{"Name", class_name}});
        if (!r.ok) {
            std::string why = "rejected";
            for (const auto& reason : r.rejections) why += " " + reason + ";";
            throw std::runtime_error(why);
        }
        return r.source;
    };
    return s;
}

namespace {

// Reads a whole file. Returns false when it cannot be opened.
bool read_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

// The project's src/ .java files the fixer may change, within a byte budget. The failing class
// always comes first. Then come classes whose names appear in the failing test or class, then the
// rest by path.
std::vector<app::fixer::SourceFile> choose_files(const std::string& project_dir,
                                                 const std::string& class_path,
                                                 const std::string& class_source,
                                                 const std::string& test_source) {
    namespace fs = std::filesystem;
    constexpr std::size_t kBudget = 12000;  // about 3000 tokens of source
    // Only paths and sizes are gathered and sorted. A file's text is read only when the budget
    // admits it, so a large project is not read in full for a prompt of a few kilobytes.
    struct Candidate {
        app::fixer::SourceFile file;  // text stays empty until it is chosen
        fs::path full;
        std::uintmax_t size = 0;
        bool referenced = false;
    };
    std::vector<Candidate> all;
    std::error_code ec;
    const fs::path src = fs::path(project_dir) / "src";
    if (!fs::is_directory(src, ec)) return {};
    for (auto it = fs::recursive_directory_iterator(src, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".java") continue;
        Candidate c;
        c.file.path = fs::relative(it->path(), project_dir, ec).generic_string();
        c.full = it->path();
        c.size = fs::file_size(c.full, ec);
        if (ec) continue;
        const std::string stem = it->path().stem().string();
        c.referenced = test_source.find(stem) != std::string::npos || class_source.find(stem) != std::string::npos;
        all.push_back(std::move(c));
    }
    std::sort(all.begin(), all.end(), [&](const Candidate& a, const Candidate& b) {
        if (a.file.path == class_path) return true;
        if (b.file.path == class_path) return false;
        if (a.referenced != b.referenced) return a.referenced;
        return a.file.path < b.file.path;
    });
    std::vector<app::fixer::SourceFile> chosen;
    std::size_t used = 0;
    for (auto& c : all) {
        if (!chosen.empty() && used + c.size > kBudget) continue;
        if (!read_file(c.full, c.file.text)) continue;
        used += c.file.text.size();
        chosen.push_back(std::move(c.file));
    }
    return chosen;
}

}  // namespace

Graph build_graph(std::string document_text, Services services, std::shared_ptr<TaskBoard> board) {
    Graph g;
    g.add_node("analyze", guarded("analyze",
        [doc = std::move(document_text)](const State&, Trace& t, Patch& p) {
            wb::Session session;
            session.set_text(doc);
            const wb::Report report = session.analyze();
            const std::size_t symbols = report.symbols.size();
            const std::size_t diagnostics = report.diagnostics.size();
            t.log("parsed " + std::to_string(doc.size()) + " bytes");
            p["symbols"] = symbols;
            p["doc_diagnostics"] = diagnostics;
            return Outcome{NodeStatus::ok, std::to_string(symbols) + " symbols, " +
                                               std::to_string(diagnostics) + " diagnostics"};
        }, board));

    g.add_node("architect", guarded("architect",
        [hook = services.architect](const State&, Trace& t, Patch& p) {
            if (!hook) return Outcome{NodeStatus::skipped, "not wired (no project folder)"};
            const app::architect::Outline o = hook();
            std::size_t logged = 0;
            for (const auto& skip : o.skipped) {
                if (logged++ == kMaxLoggedSkips) break;
                t.log("skipped " + skip.path + ": " + skip.reason);
            }
            p["architect_files"] = o.files;
            p["architect_types"] = o.types;
            p["architect_methods"] = o.methods;
            p["architect_skipped"] = o.skipped.size();
            p["architect_outline"] = outline_json(o);
            return Outcome{NodeStatus::ok, app::architect::summarize(o)};
        }, board));

    g.add_node("generate", guarded("generate",
        [gen = services.generate](const State& s, Trace& t, Patch& p) {
            if (!gen) throw std::runtime_error("no generator wired");
            const std::string cls = text(s, "class_name");
            t.log("Singleton " + cls);
            std::string source = gen(cls);
            Outcome out{NodeStatus::ok, "Singleton " + cls + ", " +
                                            std::to_string(count_lines(source)) + " lines"};
            p["source"] = source;
            return out;
        }, board));

    g.add_node("check", guarded("check",
        [](const State& s, Trace&, Patch&) {
            wb::Session session;
            session.set_text(text(s, "source"));
            const std::size_t diagnostics = session.analyze().diagnostics.size();
            if (diagnostics == 0) return Outcome{NodeStatus::ok, "0 diagnostics"};
            return Outcome{NodeStatus::failed, std::to_string(diagnostics) + " diagnostics"};
        }, board));

    g.add_node("compile", guarded("compile",
        [hook = services.compile](const State&, Trace&, Patch&) { return run_hook(hook); },
        board));

    g.add_node("test", guarded("test",
        [hook = services.test](const State&, Trace&, Patch& p) {
            Outcome out = run_hook(hook);  // a throwing hook fails the node and skips this line
            // A passing run clears the reason a failed attempt left behind, so report
            // does not say "stopped at test" after a successful fix.
            p["reason"] = std::string();
            return out;
        }, board));

    g.add_node("debug", guarded("debug",
        [fixer = services.fixer, project_dir = services.project_dir,
         check = services.check_proposal, max_attempts = services.max_fix_attempts, board](
            const State& s, Trace& t, Patch& p) {
            if (!fixer) return Outcome{NodeStatus::skipped, "not wired (no fixer)"};
            const int before = integer(s, "attempts");
            app::fixer::FailureContext ctx;
            ctx.test_output = text(s, "reason");
            ctx.project_dir = project_dir ? project_dir() : std::string();
            // The failing class is read from disk: edits are whole-file replacements of project files.
            ctx.class_path = app::fixer::class_path_for_failure(ctx.project_dir, ctx.test_output);
            if (ctx.class_path.empty()) {
                p["debug_fixed"] = false;
                return Outcome{NodeStatus::skipped, "no failing class found in the project"};
            }
            if (!read_file(std::filesystem::path(ctx.project_dir) / ctx.class_path, ctx.class_source)) {
                p["debug_fixed"] = false;
                return Outcome{NodeStatus::skipped, "cannot read " + ctx.class_path};
            }
            // The failing test, when the project has one by convention, is the spec for the fix.
            read_file(std::filesystem::path(ctx.project_dir) / app::fixer::test_path_for(ctx.class_path),
                      ctx.test_source);
            ctx.files = choose_files(ctx.project_dir, ctx.class_path, ctx.class_source, ctx.test_source);
            if (ctx.files.empty()) {
                p["debug_fixed"] = false;
                return Outcome{NodeStatus::skipped, "no project files to change"};
            }

            std::string last_reason;
            for (int k = 1; k <= std::max(1, max_attempts); ++k) {
                const int attempt = before + k;
                p["attempts"] = attempt;
                ctx.attempt = attempt;
                ctx.feedback = last_reason;
                app::fixer::Proposal proposal;
                try {
                    proposal = fixer(ctx);
                } catch (const std::exception& e) {
                    p["debug_fixed"] = false;
                    const std::string why = std::string(e.what());
                    if (k == 1) return Outcome{NodeStatus::skipped, "no proposal: " + why};
                    return Outcome{NodeStatus::failed, "no green proposal: " + why};
                } catch (...) {
                    p["debug_fixed"] = false;
                    return Outcome{NodeStatus::skipped, "no proposal: unknown exception"};
                }

                // Each edit must name a project file, and is made against that file's text.
                std::string reject;
                app::fixer::Proposal kept;
                kept.rationale = proposal.rationale;
                for (app::fixer::FileEdit e : proposal.edits) {
                    const app::fixer::SourceFile* base = nullptr;
                    for (const auto& f : ctx.files) if (f.path == e.class_path) base = &f;
                    if (!base) { reject = "reply changed a file outside the project files: " + e.class_path; break; }
                    if (e.new_source == base->text) continue;  // unchanged files are not edits
                    e.base_source = base->text;
                    kept.edits.push_back(e);
                }
                if (reject.empty() && proposal.edits.empty())
                    reject = "reply had no usable 'File: <path>' block with a ```java code block";
                if (reject.empty() && kept.edits.empty())
                    reject = "your reply made no change; the test report is still:\n" + ctx.test_output;
                if (reject.empty() && check) reject = check(kept);
                if (!reject.empty()) {
                    last_reason = reject;
                    std::string first = reject.substr(0, reject.find('\n'));
                    t.log("attempt " + std::to_string(attempt) + " rejected: " + first);
                    continue;
                }
                std::string rationale = kept.rationale;
                std::replace(rationale.begin(), rationale.end(), '\n', ' ');
                p["debug_fixed"] = true;
                board->set_proposal(kept);
                std::string files_changed;
                for (const auto& e : kept.edits) files_changed += (files_changed.empty() ? "" : ", ") + e.class_path;
                t.log("attempt " + std::to_string(attempt) + ": " + rationale + " [" + files_changed + "]");
                return Outcome{NodeStatus::ok, "attempt " + std::to_string(attempt) + ": " + rationale};
            }
            p["debug_fixed"] = false;
            std::string first = last_reason.substr(0, last_reason.find('\n'));
            return Outcome{NodeStatus::failed, "no green proposal after " +
                                                   std::to_string(std::max(1, max_attempts)) +
                                                   " attempts: " + first};
        }, board));

    g.add_node("report", guarded("report",
        [](const State& s, Trace&, Patch&) {
            std::string reason = text(s, "reason");
            std::replace(reason.begin(), reason.end(), '\n', ' ');
            std::string summary;
            if (flag(s, "debug_fixed")) {
                // A green proposal exists: the run did not stop, it is waiting for a human to apply.
                summary = "proposal ready, not applied; before the fix: " + reason;
            } else if (reason.empty()) {
                summary = "all steps passed";
            } else {
                summary = "stopped at " + reason;
            }
            const int attempts = integer(s, "attempts");
            if (attempts > 0) summary += "; fix attempts " + std::to_string(attempts);
            return Outcome{NodeStatus::ok, summary};
        }, board));

    g.set_start("analyze");
    g.add_edge("analyze", "architect");
    g.add_conditional_edge("architect", [](const State& s) {
        return flag(s, "architect_ok") ? std::string("generate") : std::string("report");
    });
    g.add_conditional_edge("generate", [](const State& s) {
        return flag(s, "generate_ok") ? std::string("check") : std::string("report");
    });
    g.add_conditional_edge("check", [](const State& s) {
        return flag(s, "check_ok") ? std::string("compile") : std::string("report");
    });
    g.add_conditional_edge("compile", [](const State& s) {
        return flag(s, "compile_ok") ? std::string("test") : std::string("report");
    });
    // A passing test (or a skipped one) goes to report. A failing test goes to debug.
    g.add_conditional_edge("test", [](const State& s) {
        return flag(s, "test_ok") ? std::string("report") : std::string("debug");
    });
    // Debug never applies a fix, so there is nothing to retest: the run always reports.
    g.add_edge("debug", "report");
    g.add_edge("report", wb::graph::kEnd);
    return g;
}

// ---- TaskBoard ---------------------------------------------------------------

void TaskBoard::reset() {
    std::lock_guard<std::mutex> lock(mu_);
    snap_ = Snapshot{};
    snap_.running = true;
    for (const auto& name : node_names()) {
        NodeRow row;
        row.name = name;
        snap_.nodes.push_back(row);
    }
    ++version_;
}

void TaskBoard::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    snap_ = Snapshot{};
    ++version_;
}

void TaskBoard::set_row(const std::string& node, NodeStatus status, double duration_ms,
                        const std::string& summary) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& row : snap_.nodes) {
        if (row.name != node) continue;
        row.status = status;
        row.duration_ms = duration_ms;
        row.summary = summary;
        break;
    }
    ++version_;
}

void TaskBoard::finish(const wb::graph::Result& result) {
    std::lock_guard<std::mutex> lock(mu_);
    snap_.running = false;
    snap_.finished = true;
    snap_.outcome = wb::graph::to_string(result.status);
    snap_.error = result.error;
    snap_.log.clear();
    for (const auto& e : result.trace.events()) {
        snap_.log.push_back(LogLine{e.seq, e.node, wb::graph::to_string(e.kind), e.message});
    }
    snap_.dropped = result.trace.dropped();
    snap_.total_recorded = result.trace.total_recorded();
    ++version_;
}

void TaskBoard::set_proposal(const app::fixer::Proposal& proposal) {
    std::lock_guard<std::mutex> lock(mu_);
    snap_.proposal = proposal;
    ++version_;
}

void TaskBoard::fail(const std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    snap_.running = false;
    snap_.finished = true;
    snap_.outcome = "failed";
    snap_.error = error;
    ++version_;
}

bool TaskBoard::running() const {
    std::lock_guard<std::mutex> lock(mu_);
    return snap_.running;
}

std::uint64_t TaskBoard::version() const {
    std::lock_guard<std::mutex> lock(mu_);
    return version_;
}

Snapshot TaskBoard::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return snap_;
}

// ---- TaskRunner --------------------------------------------------------------

TaskRunner::TaskRunner() : board_(std::make_shared<TaskBoard>()) {}

TaskRunner::~TaskRunner() {
    if (worker_.joinable()) worker_.join();
}

bool TaskRunner::start(std::string document_text, std::string class_name, Services services,
                       wb::graph::Options options) {
    poll();
    if (board_->running()) return false;

    board_->reset();
    Graph graph = build_graph(std::move(document_text), std::move(services), board_);
    State state;
    state.set("class_name", class_name);

    worker_ = std::thread([board = board_, graph = std::move(graph), state = std::move(state),
                           options]() mutable {
        try {
            board->finish(graph.run(std::move(state), options));
        } catch (const std::exception& e) {
            board->fail(e.what());
        } catch (...) {
            board->fail("unknown exception");
        }
    });
    return true;
}

void TaskRunner::poll() {
    if (worker_.joinable() && !board_->running()) worker_.join();
}

void TaskRunner::clear() {
    poll();
    if (!board_->running()) board_->clear();
}

}  // namespace app::task
