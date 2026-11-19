#pragma once

// Pluggable fixer interface. Pure: no ImGui, no file writes. The workbench ships with no
// fixer wired (see main.cpp); a model provider would be one implementation of Fixer.

#include <functional>
#include <string>

namespace app::fixer {

// What a fixer sees about a failed test run.
struct FailureContext {
    std::string project_dir;
    std::string class_path;    // project-relative, e.g. "src/demo/Cart.java"; may be empty
    std::string class_source;  // text the proposal will be made against
    std::string test_output;   // failure report, e.g. "1 passed, 1 failed; FAIL demo.CartTest.x"
    int attempt = 0;
};

// A whole-file replacement for one class under <project>/src.
struct Proposal {
    std::string class_path;  // project-relative, must start with "src/"
    std::string new_source;
    std::string rationale;
    // Text the proposal was made against. apply() refuses when the file on disk differs.
    std::string base_source;
};

// Returns a proposal, or throws when it has none.
using Fixer = std::function<Proposal(const FailureContext&)>;

// Parses the first "FAIL <pkg>.<Name>Test.<method>" in `test_output` and maps it to
// "src/<pkg path>/<Name>.java". Returns that project-relative path when the file exists
// under `project_dir`, and an empty string otherwise.
std::string class_path_for_failure(const std::string& project_dir, const std::string& test_output);

}  // namespace app::fixer
