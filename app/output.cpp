#include "output.hpp"

#include <string>

#include "imgui.h"

namespace app {

void draw_output_panel(CheckState& state, wb::Verifier& verifier, const Document* doc) {
    ImGui::TextUnformatted("Output: Check your Java");
    ImGui::Separator();

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

}  // namespace app
