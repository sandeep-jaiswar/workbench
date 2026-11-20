#pragma once

#include <cstddef>

#include "chat.hpp"

namespace app {

// UI-only state of the chat panel: the input line and scroll bookkeeping.
struct ChatPanel {
    char input[2048] = "";
    bool focus_input = true;
    std::size_t seen_turns = 0;
};

// Chat log, input line and Send. Returns true when the Task trace button was pressed.
bool draw_chat_panel(ChatPanel& panel, chat::ChatState& chat, chat::CommandEnv& env);

}  // namespace app
