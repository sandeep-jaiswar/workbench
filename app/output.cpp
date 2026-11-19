#include "output.hpp"

#include <string>
#include <utility>

#include "imgui.h"
#include "test_gen.hpp"

namespace fs = std::filesystem;

namespace app {

JavaRun::~JavaRun() {
    if (worker_.joinable()) worker_.join();
}

void JavaRun::start(const std::string& title, std::function<wbx::RunResult()> job) {
    if (busy_) return;
    title_ = title;
    busy_ = true;
    has_result_ = false;
    diagnostics_.clear();
    open_error.clear();
    worker_ = std::thread([this, job = std::move(job)] {
        wbx::RunResult r = job();
        std::lock_guard<std::mutex> lock(mu_);
        pending_ = std::move(r);
        done_ = true;
    });
}

void JavaRun::poll() {
    if (!busy_) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!done_) return;
        shown_ = std::move(pending_);
        pending_ = wbx::RunResult{};
        done_ = false;
    }
    worker_.join();
    busy_ = false;
    has_result_ = true;
    diagnostics_ = wbx::parse_javac_output(shown_.output);
}

namespace {

void draw_build_tab(JavaRun& run, Editor& editor, const fs::path& root) {
    const bool busy = run.busy();
    const std::string root_str = root.string();

    ImGui::BeginDisabled(busy || root.empty());
    if (ImGui::Button("Build")) {
        run.start("Build", [root_str] { return wbx::compile_project(root_str); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Test")) {
        run.start("Test", [root_str] { return wbx::run_tests(root_str, "demo.TestRunner"); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Tests for missing classes")) {
        run.start("Tests for missing classes", [root_str] {
            const app::test_gen::Report report = app::test_gen::generate_missing_tests(root_str);
            wbx::RunResult result;
            result.exit_code = report.failed ? 1 : 0;
            result.output = app::test_gen::format_report(report, root_str);
            return result;
        });
    }
    ImGui::EndDisabled();

    if (root.empty()) {
        ImGui::TextDisabled("Open a project folder first.");
    } else if (busy) {
        ImGui::Text("%s running...", run.title().c_str());
    } else if (run.has_result()) {
        const wbx::RunResult& r = run.result();
        if (r.timed_out) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s timed out", run.title().c_str());
        } else {
            ImGui::Text("%s finished, exit %d", run.title().c_str(), r.exit_code);
        }
    }

    const float avail = ImGui::GetContentRegionAvail().y;
    const bool has_diags = !run.diagnostics().empty();
    const float out_h = has_diags ? avail * 0.6f : avail;
    ImGui::BeginChild("run_output", ImVec2(0, out_h), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (run.has_result()) ImGui::TextUnformatted(run.result().output.c_str());
    ImGui::EndChild();

    if (!run.open_error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", run.open_error.c_str());

    if (has_diags) {
        ImGui::BeginChild("diagnostics", ImVec2(0, 0), true);
        const auto& diags = run.diagnostics();
        for (std::size_t i = 0; i < diags.size(); ++i) {
            const wbx::Diagnostic& d = diags[i];
            const std::string name = fs::path(d.file).filename().string();
            const std::string label = name + ":" + std::to_string(d.line) + "  " + d.message;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(label.c_str())) {
                // javac prints the paths it was given, which are absolute. Relative
                // paths are resolved against the project root.
                fs::path file = d.file;
                if (file.is_relative()) file = root / file;
                // The caret goes to the column javac marked with '^' (1 when it printed none).
                std::string error;
                const std::size_t line = d.line > 0 ? static_cast<std::size_t>(d.line) : 1;
                const std::size_t column = d.column > 0 ? static_cast<std::size_t>(d.column) : 1;
                run.open_error = editor.goto_line(file, line, column, error) ? std::string() : error;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}

void draw_syntax_tab(CheckState& state, wb::Verifier& verifier, const Document* doc) {
    if (doc == nullptr) {
        ImGui::TextDisabled("Open a .java file to check its syntax.");
        return;
    }
    ImGui::Text("File: %s", doc->name().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Check syntax")) {
        const wb::Analysis a = verifier.analyze(doc->text());
        state.issues = a.errors;
        if (a.ok) {
            state.summary = "No syntax errors";
        } else if (a.errors.empty()) {
            state.summary = "Syntax check failed";
        } else {
            state.summary = std::to_string(a.errors.size()) + " syntax error(s), first at line " +
                            std::to_string(a.errors.front().line);
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(state.summary.c_str());

    ImGui::BeginChild("issues", ImVec2(0, 0), false);
    for (const wb::SyntaxIssue& issue : state.issues) {
        const char* kind = issue.missing ? "missing" : "error";
        ImGui::Text("line %u: %s %s", issue.line, kind, issue.node_type.c_str());
    }
    ImGui::EndChild();
}

}  // namespace

void draw_output_panel(CheckState& state, wb::Verifier& verifier, const Document* doc,
                       JavaRun& run, Editor& editor, const fs::path& project_root) {
    run.poll();
    ImGui::TextUnformatted("Output");
    ImGui::Separator();
    if (ImGui::BeginTabBar("output_tabs")) {
        if (ImGui::BeginTabItem("Build and test")) {
            draw_build_tab(run, editor, project_root);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Syntax")) {
            draw_syntax_tab(state, verifier, doc);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

}  // namespace app
