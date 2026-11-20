// Headless tests for the chat console. Links wb_core only: no ImGui, GLFW or display.
// Usage: chat_test [catalog.json]. With a catalog, the command cases also run against it.
// CHECK is used instead of assert because Release builds define NDEBUG.
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "catalog.hpp"
#include "chat.hpp"
#include "verify.hpp"

namespace {

using app::chat::ChatState;
using app::chat::CommandEnv;
using app::chat::Turn;

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                             \
            ++failures;                                                \
        }                                                              \
    } while (0)

bool contains(const std::string& s, const std::string& part) {
    return s.find(part) != std::string::npos;
}

void test_split() {
    const auto words = app::chat::split_command("  /generate   Singleton  Cart ");
    CHECK((words == std::vector<std::string>{"/generate", "Singleton", "Cart"}));
    CHECK(app::chat::split_command("   ").empty());
}

void test_budget() {
    std::vector<Turn> history = {
        {true, false, std::string(100, 'a')},
        {false, false, std::string(100, 'b')},
        {true, false, std::string(100, 'c')},
        {false, true, "should be skipped because it is an error"},
    };
    // Room for two turns of about 110 bytes each: the oldest one is dropped.
    const std::string msg = app::chat::build_user_message(history, "why?", 230);
    CHECK(!contains(msg, std::string(100, 'a')));
    CHECK(contains(msg, std::string(100, 'b')));
    CHECK(contains(msg, std::string(100, 'c')));
    CHECK(!contains(msg, "should be skipped"));
    CHECK(contains(msg, "Question: why?"));

    // No history: the message is just the question.
    CHECK(app::chat::build_user_message({}, "hi", 1000) == "Question: hi");
}

void test_commands(const wb::Catalog* catalog) {
    wb::Verifier verifier;
    std::string class_name = "Demo";
    const std::string bad_doc = "public class {{";
    const std::string good_doc = "public class Demo { }";

    CommandEnv env{*catalog, verifier, class_name, nullptr};
    CHECK(contains(app::chat::run_command("/help", env), "/generate"));
    CHECK(contains(app::chat::run_command("/nope", env), "unknown command"));
    CHECK(contains(app::chat::run_command("/check", env), "No file is open"));

    CHECK(contains(app::chat::run_command("/name Cart", env), "Class name set to Cart"));
    CHECK(class_name == "Cart");
    CHECK(contains(app::chat::run_command("/name 1bad", env), "not a valid class name"));
    CHECK(class_name == "Cart");

    CHECK(contains(app::chat::run_command("/patterns", env), "Singleton"));
    CHECK(contains(app::chat::run_command("/generate nosuch", env), "unknown pattern"));
    CHECK(contains(app::chat::run_command("/generate", env), "usage: /generate"));
    CHECK(contains(app::chat::run_command("/generate Singleton 2bad", env),
                   "not a valid class name"));

    // By id or by name, and the class name defaults to /name.
    CHECK(contains(app::chat::run_command("/generate Singleton", env), "Verified Singleton as Cart"));
    CHECK(contains(app::chat::run_command("/generate singleton Other", env),
                   "Verified Singleton as Other"));

    CommandEnv with_doc{*catalog, verifier, class_name, &bad_doc};
    CHECK(contains(app::chat::run_command("/check", with_doc), "syntax error"));
    CommandEnv with_good{*catalog, verifier, class_name, &good_doc};
    CHECK(contains(app::chat::run_command("/check", with_good), "No syntax errors"));
}

// Waits for a worker reply, polling the way the UI does. Gives up after five seconds.
void wait_for_reply(ChatState& chat) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (chat.busy() && std::chrono::steady_clock::now() < deadline) {
        chat.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    chat.poll();
}

void test_chat_flow(const wb::Catalog& catalog) {
    wb::Verifier verifier;
    std::string class_name = "Demo";
    CommandEnv env{catalog, verifier, class_name, nullptr};

    // No model: free text gets a note, nothing is sent to a worker.
    ChatState no_model;
    no_model.send("hello", env);
    CHECK(no_model.turns.size() == 2);
    CHECK(no_model.turns.size() == 2 && no_model.turns[1].error);
    CHECK(!no_model.busy());
    no_model.send("/clear", env);
    CHECK(no_model.turns.empty());

    // Commands run at once and do not need a responder.
    no_model.send("/help", env);
    CHECK(no_model.turns.size() == 2 && contains(no_model.turns[1].text, "Commands"));

    // With a responder: the reply comes back through poll(), and the system prompt is passed.
    std::vector<std::string> seen_systems;
    std::vector<std::string> seen_users;
    ChatState chat;
    chat.responder = [&](const std::string& system, const std::string& user) {
        seen_systems.push_back(system);
        seen_users.push_back(user);
        return std::string("reply to: ") + user.substr(user.find("Question: "));
    };
    chat.send("first", env);
    CHECK(chat.busy());
    wait_for_reply(chat);
    CHECK(!chat.busy());
    CHECK(chat.turns.size() == 2);
    CHECK(chat.turns.size() == 2 && chat.turns[1].text == "reply to: Question: first");
    CHECK(seen_systems.size() == 1 && seen_systems[0] == app::chat::system_prompt());

    // The second question carries the first exchange as history.
    chat.send("second", env);
    wait_for_reply(chat);
    CHECK(seen_users.size() == 2);
    CHECK(seen_users.size() == 2 && contains(seen_users[1], "User: first"));
    CHECK(seen_users.size() == 2 && contains(seen_users[1], "Assistant: reply to: Question: first"));
    CHECK(chat.turns.size() == 4);

    // A failing responder becomes an error turn and does not stop the chat.
    chat.responder = [](const std::string&, const std::string&) -> std::string {
        throw std::runtime_error("context overflow");
    };
    chat.send("third", env);
    wait_for_reply(chat);
    CHECK(chat.turns.size() == 6);
    CHECK(chat.turns.size() == 6 && chat.turns[5].error);
    CHECK(chat.turns.size() == 6 && chat.turns[5].text == "context overflow");
}

}  // namespace

int main(int argc, char** argv) {
    // Commands that use the catalog need one. The rest run without it.
    wb::Catalog catalog;
    bool have_catalog = false;
    if (argc > 1) {
        try {
            catalog = wb::Catalog::load(argv[1]);
            have_catalog = true;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "catalog: %s\n", e.what());
            return 1;
        }
    } else {
        std::fprintf(stderr, "note: no catalog given; command cases that need one are skipped\n");
    }

    test_split();
    test_budget();
    test_chat_flow(catalog);  // an empty catalog is fine: the flow cases use no patterns
    if (have_catalog) test_commands(&catalog);

    if (failures == 0) std::printf("chat_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
