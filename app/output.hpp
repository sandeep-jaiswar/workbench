#pragma once

#include <string>
#include <vector>

#include "document.hpp"
#include "verify.hpp"

namespace app {

// Result of the last syntax check. Runs only on button click, never per frame.
struct CheckState {
    std::string summary;
    std::vector<wb::SyntaxIssue> issues;
};

// Output panel: checks the active file's Java syntax and lists the problems.
void draw_output_panel(CheckState& state, wb::Verifier& verifier, const Document* doc);

}  // namespace app
