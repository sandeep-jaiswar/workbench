#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "document.hpp"
#include "editor.hpp"
#include "java_tools.hpp"
#include "verify.hpp"

namespace app {

// Result of the last syntax check. Runs only on button click, never per frame.
struct CheckState {
    std::string summary;
    std::vector<wb::SyntaxIssue> issues;
};

// One javac or java run on a worker thread. The worker stores its result under `mu_`;
// the UI thread collects it in poll() once per frame. Only one run at a time.
class JavaRun {
public:
    JavaRun() = default;
    ~JavaRun();
    JavaRun(const JavaRun&) = delete;
    JavaRun& operator=(const JavaRun&) = delete;

    bool busy() const { return busy_; }
    void start(const std::string& title, std::function<wbx::RunResult()> job);
    void poll();  // UI thread: takes a finished result and parses its diagnostics

    const std::string& title() const { return title_; }
    bool has_result() const { return has_result_; }
    const wbx::RunResult& result() const { return shown_; }
    const std::vector<wbx::Diagnostic>& diagnostics() const { return diagnostics_; }

    std::string open_error;  // set when a diagnostic's file cannot be opened

private:
    std::mutex mu_;
    bool done_ = false;             // guarded by mu_
    wbx::RunResult pending_;        // guarded by mu_
    std::thread worker_;
    bool busy_ = false;             // UI thread only
    bool has_result_ = false;
    std::string title_;
    wbx::RunResult shown_;
    std::vector<wbx::Diagnostic> diagnostics_;
};

// Output panel: Build and Test for the project, and the syntax check for the active file.
// `project_root` is the folder open in the project tree (empty when none).
void draw_output_panel(CheckState& state, wb::Verifier& verifier, const Document* doc,
                       JavaRun& run, Editor& editor, const std::filesystem::path& project_root);

}  // namespace app
