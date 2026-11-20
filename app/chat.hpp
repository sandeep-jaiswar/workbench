#pragma once

// The chat console. Headless: no ImGui, no GLFW. Slash commands run on the calling thread and use
// java-wasm's catalog, generator and verifier directly. Free text goes to a Responder, which the
// caller wires to the local model (java-wasm's llm module) and which runs on a worker thread.

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "catalog.hpp"
#include "verify.hpp"

namespace app::chat {

struct Turn {
    bool from_user = false;
    bool error = false;
    std::string text;
};

// Answers one question. Receives the system prompt and the user message (history included).
// Returns the reply text, or throws std::exception on failure. Called on a worker thread.
using Responder = std::function<std::string(const std::string& system, const std::string& user)>;

// What a slash command may read and change.
struct CommandEnv {
    const wb::Catalog& catalog;
    wb::Verifier& verifier;          // UI thread only
    std::string& class_name;         // set by /name, used by /generate and the task trace
    const std::string* doc_text;     // the active file, or nullptr when none is open
};

// Splits "/generate Singleton Cart" into {"generate", "Singleton", "Cart"}.
std::vector<std::string> split_command(const std::string& line);

// Runs one "/command" line and returns the reply. Unknown commands return the help text.
std::string run_command(const std::string& line, CommandEnv& env);

// The system prompt for free-text questions.
std::string system_prompt();

// Builds the user message: the most recent turns that fit `budget` bytes, then the question.
// Older turns are dropped first so the prompt stays inside the model's context window.
std::string build_user_message(const std::vector<Turn>& history, const std::string& question,
                               std::size_t budget);

class ChatState {
public:
    ChatState() = default;
    ~ChatState();
    ChatState(const ChatState&) = delete;
    ChatState& operator=(const ChatState&) = delete;

    std::vector<Turn> turns;
    std::string class_name = "Demo";
    // Set by the caller. Empty when no model is loaded; free text then gets a note instead.
    Responder responder;

    // Sends one line from the input box. Commands finish at once; free text starts a worker.
    // Ignored while a reply is pending.
    void send(const std::string& line, CommandEnv& env);

    // Collects a finished reply. Call once per frame on the UI thread.
    void poll();

    bool busy() const { return busy_; }

private:
    bool busy_ = false;
    std::thread worker_;
    std::mutex mu_;
    bool done_ = false;
    Turn pending_;
};

}  // namespace app::chat
