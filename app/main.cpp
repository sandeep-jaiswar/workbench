// Workbench: Dear ImGui desktop front end for the java-wasm pipeline.
// Entry point and root layout. Window and frame loop: app_window.cpp. Panels: *_panel, editor,
// output and generate_panel sources.
#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

#include "imgui.h"

#include "app_window.hpp"
#include "architect.hpp"
#include "catalog.hpp"
#include "editor.hpp"
#include "generate_panel.hpp"
#include "outline.hpp"
#include "output.hpp"
#include "project_panel.hpp"
#include "java_tools.hpp"
#include "task_trace.hpp"
#include "llm.hpp"
#include "llm_fixer.hpp"
#include "apply_fix.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include "trace_panel.hpp"
#include "verify.hpp"

#ifndef WB_DEFAULT_CATALOG
#define WB_DEFAULT_CATALOG "catalog/patterns.json"
#endif

namespace {

constexpr float kProjectWidth = 260.0f;
constexpr float kGenerateWidth = 360.0f;
constexpr float kOutputHeight = 190.0f;
constexpr float kOutlineShare = 0.4f;  // share of the left column given to the outline

// "N passed, M failed; FAIL a, b" for the failure report. Long lists are cut after five names.
// The last `max` bytes of `s`, starting at a line boundary.
std::string clip_tail(const std::string& s, std::size_t max) {
    if (s.size() <= max) return s;
    const std::size_t from = s.find('\n', s.size() - max);
    return from == std::string::npos ? s.substr(s.size() - max) : s.substr(from + 1);
}

// The "FAIL ..." lines of a test run, each with its assertion message, capped in size.
std::string failure_details(const std::string& output) {
    std::string out;
    std::size_t at = 0;
    while (at < output.size() && out.size() < 2000) {
        std::size_t end = output.find('\n', at);
        if (end == std::string::npos) end = output.size();
        const std::string line = output.substr(at, end - at);
        if (line.compare(0, 5, "FAIL ") == 0) out += "\n" + line;
        at = end + 1;
    }
    return out;
}

std::string describe_tests(const wbx::TestResult& t) {
    std::string s = std::to_string(t.passed) + " passed, " + std::to_string(t.failed) + " failed";
    if (t.failing.empty()) return s;
    s += "; FAIL ";
    for (std::size_t i = 0; i < t.failing.size() && i < 5; ++i) {
        if (i > 0) s += ", ";
        s += t.failing[i];
    }
    if (t.failing.size() > 5) s += " (+" + std::to_string(t.failing.size() - 5) + " more)";
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string catalog_path = argc > 1 ? argv[1] : WB_DEFAULT_CATALOG;

    wb::Catalog catalog;
    try {
        catalog = wb::Catalog::load(catalog_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }

    app::AppWindow window;
    if (!window.init(1280, 760, "Workbench")) return 1;

    wb::Verifier verifier;
    app::ProjectPanel project;
    app::Editor editor;
    app::Outline outline;
    app::GenerateState generate;
    app::CheckState check;
    app::JavaRun java_run;
    app::TracePanel trace;
    trace.services = app::task::make_services(catalog);
    // Compile and test run the project folder through javac and the sample test runner. A non-zero
    // exit throws, so the task graph records the node as failed and routes to the report step.
    trace.services.compile = [&project]() -> std::string {
        const std::string dir = project.tree.root().path.string();
        if (dir.empty()) throw std::runtime_error("open a project folder first");
        const wbx::RunResult r = wbx::compile_project(dir);
        if (r.exit_code != 0) throw std::runtime_error("javac exit " + std::to_string(r.exit_code));
        return "javac exit 0";
    };
    trace.services.test = [&project]() -> std::string {
        const std::string dir = project.tree.root().path.string();
        if (dir.empty()) throw std::runtime_error("open a project folder first");
        const wbx::RunResult r = wbx::run_tests(dir, "demo.TestRunner");
        const wbx::TestResult t = wbx::parse_test_output(r.output);
        if (r.exit_code == 0) return describe_tests(t);
        // No counts in the output means the run never reached the tests (javac or timeout).
        if (t.failed == 0 && t.failing.empty()) {
            throw std::runtime_error(r.timed_out ? std::string("tests timed out")
                                                 : "tests exit " + std::to_string(r.exit_code));
        }
        // The FAIL lines carry each assertion message; the debug step needs them to propose a fix.
        throw std::runtime_error(describe_tests(t) + failure_details(r.output));
    };
    // Stages a proposal in a copy of the project. Empty means green; otherwise the reason.
    trace.services.check_proposal = [&project](const app::fixer::Proposal& prop) -> std::string {
        const std::string dir = project.tree.root().path.string();
        if (dir.empty()) return "open a project folder first";
        const app::apply_fix::StageResult staged = app::apply_fix::stage(dir, prop);
        if (!staged.error.empty()) return "staging failed: " + staged.error;
        if (!staged.compiled) return "javac failed:\n" + clip_tail(staged.compile_output, 1500);
        if (!staged.tested) return "tests failed:\n" + clip_tail(staged.test_output, 1500);
        return "";
    };
    // The project's outline for the architect step.
    trace.services.architect = [&project]() -> app::architect::Outline {
        const std::string dir = project.tree.root().path.string();
        if (dir.empty()) throw std::runtime_error("open a project folder first");
        return app::architect::outline_project(dir);
    };
    trace.services.project_dir = [&project]() -> std::string {
        return project.tree.root().path.string();
    };
    // Local model for the debug step. WB_MODEL=/path/to/model.gguf overrides the default model,
    // which scripts/fetch-model.sh installs. With no model file the debug step is skipped.
    std::shared_ptr<wb::llm::Model> model;
    const char* env_model = std::getenv("WB_MODEL");
    const std::string model_path = (env_model && *env_model) ? env_model : WB_DEFAULT_MODEL;
    if (std::FILE* f = std::fopen(model_path.c_str(), "rb")) {
        std::fclose(f);
        wb::llm::Options llm_opt;
        llm_opt.n_ctx = 8192;
        llm_opt.max_new_tokens = 2048;
        llm_opt.n_gpu_layers = WB_LLM_GPU_LAYERS;
        std::string llm_error;
        model = wb::llm::Model::load(model_path, llm_opt, &llm_error);
        if (model) {
            trace.services.fixer = app::fixer::make_llm_fixer(model);
            std::fprintf(stderr, "fixer: local model loaded (%s)\n", model_path.c_str());
        } else {
            std::fprintf(stderr, "fixer: model not loaded: %s\n", llm_error.c_str());
        }
    } else {
        std::fprintf(stderr, "fixer: no model at %s; debug step skipped (see README)\n",
                     model_path.c_str());
    }
    bool show_trace = false;
    project.open_folder();

    window.run([&] {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("Workbench", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float center_w = avail.x - kProjectWidth - kGenerateWidth - 2.0f * spacing;

        // Project tree on top, outline of the active file below it.
        ImGui::BeginGroup();
        const float outline_h = std::max(120.0f, avail.y * kOutlineShare);
        ImGui::BeginChild("project", ImVec2(kProjectWidth, avail.y - outline_h - ImGui::GetStyle().ItemSpacing.y), true);
        app::draw_project_panel(project, [&](const std::filesystem::path& file) {
            std::string error;
            if (!editor.open_file(file, error)) project.status = error;
        });
        ImGui::EndChild();
        ImGui::BeginChild("outline", ImVec2(kProjectWidth, 0), true);
        outline.draw(editor.active(), editor);
        ImGui::EndChild();
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::BeginChild("center", ImVec2(center_w, 0), false);
        ImGui::BeginChild("editor", ImVec2(0, avail.y - kOutputHeight - spacing), true);
        editor.draw();
        ImGui::EndChild();
        ImGui::BeginChild("output", ImVec2(0, 0), true);
        app::draw_output_panel(check, verifier, editor.active(), java_run, editor,
                               project.tree.is_open() ? project.tree.root().path
                                                      : std::filesystem::path());
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("generate", ImVec2(kGenerateWidth, 0), true);
        if (ImGui::Button("Task trace")) show_trace = !show_trace;
        app::draw_generate_panel(generate, catalog, verifier);
        ImGui::EndChild();

        if (show_trace) {
            ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 300.0f, vp->WorkPos.y + 120.0f),
                                    ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(640.0f, 460.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Task trace", &show_trace)) {
                app::draw_trace_panel(trace, editor.active(), generate.name,
                                      project.tree.is_open() ? project.tree.root().path.string()
                                                             : std::string());
            }
            ImGui::End();
        }

        ImGui::End();
    });
    return 0;
}
