#pragma once

// Pluggable fixer interface. Pure: no ImGui, no file writes. The workbench ships with no
// fixer wired (see main.cpp); a model provider would be one implementation of Fixer.

#include <functional>
#include <string>
#include <vector>

namespace app::fixer {

// A project file the fixer may read, and change.
struct SourceFile {
    std::string path;  // project-relative, e.g. "src/demo/Cart.java"
    std::string text;
};

// What a fixer sees about a failed test run.
struct FailureContext {
    std::string project_dir;
    std::string class_path;    // the class the failing test points at; may be empty
    std::string class_source;  // its text
    std::string test_output;   // failure report with the FAIL lines and assertion messages
    std::string test_source;   // the failing test class, when it exists; may be empty
    std::string feedback;      // why the previous proposal was rejected; empty on the first try
    int attempt = 0;
    // Every src/ file the proposal may change, the failing class among them. Chosen by the debug
    // step within a size budget.
    std::vector<SourceFile> files;
};

// One whole-file replacement under <project>/src.
struct FileEdit {
    std::string class_path;  // project-relative, must start with "src/"
    std::string new_source;
    std::string base_source;  // the text the edit was made against; apply() checks it on disk
};

// A set of edits that go together. Staged and applied as one unit.
struct Proposal {
    std::vector<FileEdit> edits;
    std::string rationale;
};

// Returns a proposal, or throws when it has none.
using Fixer = std::function<Proposal(const FailureContext&)>;

// Parses the first "FAIL <pkg>.<Name>Test.<method>" in `test_output` and maps it to
// "src/<pkg path>/<Name>.java". Returns that project-relative path when the file exists
// under `project_dir`, and an empty string otherwise.
std::string class_path_for_failure(const std::string& project_dir, const std::string& test_output);

// "src/<pkg>/<Name>.java" -> "test/<pkg>/<Name>Test.java" (the test class by convention).
std::string test_path_for(const std::string& class_path);

}  // namespace app::fixer
