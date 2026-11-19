#include "generate_panel.hpp"

#include <cfloat>
#include <exception>

#include "imgui.h"

namespace app {

void draw_generate_panel(GenerateState& state, const wb::Catalog& catalog, wb::Verifier& verifier) {
    ImGui::TextUnformatted("Generate a pattern");
    ImGui::Separator();

    const auto& patterns = catalog.patterns();
    if (patterns.empty()) {
        ImGui::TextUnformatted("The catalog has no patterns.");
        return;
    }
    if (state.selected >= static_cast<int>(patterns.size())) state.selected = 0;

    if (ImGui::BeginCombo("Pattern", patterns[state.selected].name.c_str())) {
        for (int i = 0; i < static_cast<int>(patterns.size()); ++i) {
            if (ImGui::Selectable(patterns[i].name.c_str(), i == state.selected)) state.selected = i;
        }
        ImGui::EndCombo();
    }
    ImGui::InputText("Class name", state.name, sizeof(state.name));
    if (ImGui::Button("Generate and verify")) {
        try {
            const auto r = wb::generate(verifier, patterns[state.selected], {{"Name", state.name}});
            if (r.ok) {
                state.output = r.source;
                state.status = "Verified";
            } else {
                state.output.clear();
                state.status = "Rejected: ";
                for (const auto& why : r.rejections) state.status += why + " | ";
            }
        } catch (const std::exception& e) {
            state.output.clear();
            state.status = std::string("Error: ") + e.what();
        }
    }
    ImGui::SameLine();
    ImGui::TextColored(state.status.rfind("Verified", 0) == 0 ? ImVec4(0.3f, 0.9f, 0.4f, 1)
                                                              : ImVec4(0.95f, 0.45f, 0.45f, 1),
                       "%s", state.status.c_str());
    ImGui::InputTextMultiline("##output", state.output.data(), state.output.size() + 1,
                              ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_ReadOnly);
}

}  // namespace app
