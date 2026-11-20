#include "chat_panel.hpp"

#include <string>

#include "imgui.h"

namespace app {

bool draw_chat_panel(ChatPanel& panel, chat::ChatState& chat, chat::CommandEnv& env) {
    bool trace_clicked = false;

    ImGui::TextUnformatted("Chat");
    ImGui::SameLine();
    if (ImGui::Button("Task trace")) trace_clicked = true;
    ImGui::SameLine();
    ImGui::TextDisabled("%s", chat.responder ? "model ready" : "no model (commands only)");
    ImGui::Separator();

    // The log takes all the height except one row for the input line.
    const float input_row = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("chat_log", ImVec2(0, -input_row), true);
    for (const chat::Turn& t : chat.turns) {
        if (t.from_user) {
            ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1), "you");
        } else if (t.error) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1), "error");
        } else {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1), "java-wasm");
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(t.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    if (chat.busy()) ImGui::TextDisabled("thinking...");

    // Follow the newest message while a reply is coming in or when a new turn arrived.
    const bool grew = chat.turns.size() != panel.seen_turns;
    panel.seen_turns = chat.turns.size();
    if (grew || chat.busy()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    if (panel.focus_input && !chat.busy()) {
        ImGui::SetKeyboardFocusHere();
        panel.focus_input = false;
    }
    const float send_w = ImGui::CalcTextSize("Send").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(send_w + ImGui::GetStyle().ItemSpacing.x));
    ImGui::BeginDisabled(chat.busy());
    const bool entered = ImGui::InputText("##message", panel.input, sizeof(panel.input),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool clicked = ImGui::Button("Send");
    ImGui::EndDisabled();

    if ((entered || clicked) && !chat.busy()) {
        chat.send(panel.input, env);
        panel.input[0] = '\0';
        panel.focus_input = true;
    }
    return trace_clicked;
}

}  // namespace app
