#include "chat.hpp"

#include <cctype>
#include <cstddef>
#include <exception>
#include <sstream>
#include <stdexcept>

#include "engine.hpp"

namespace app::chat {

namespace {

// Bytes of earlier conversation sent with each question. The local model is single-turn, and its
// context is 8192 tokens by default, so the history is kept well under that.
constexpr std::size_t kHistoryBudget = 3000;

std::string help_text() {
    return "Commands:\n"
           "  /patterns                     list the catalog patterns\n"
           "  /generate <pattern> [Class]   generate and verify a pattern (Class defaults to the class name)\n"
           "  /check                        syntax check of the active file\n"
           "  /name <Class>                 set the class name used by /generate and the task trace\n"
           "  /clear                        clear the conversation\n"
           "  /help                         this list\n"
           "Any other text is a question for the local model.";
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const wb::Pattern* find_pattern(const wb::Catalog& catalog, const std::string& key) {
    if (const wb::Pattern* p = catalog.find(key)) return p;
    const std::string want = lower(key);
    for (const auto& p : catalog.patterns()) {
        if (lower(p.name) == want) return &p;
    }
    return nullptr;
}

std::string cmd_patterns(const wb::Catalog& catalog) {
    if (catalog.patterns().empty()) return "The catalog has no patterns.";
    std::string out = "Patterns:";
    for (const auto& p : catalog.patterns()) {
        out += "\n  " + p.id + "  (" + p.name + ")";
    }
    return out;
}

std::string cmd_generate(const std::vector<std::string>& args, CommandEnv& env) {
    if (args.size() < 2 || args.size() > 3) return "usage: /generate <pattern> [ClassName]";
    const std::string cls = args.size() == 3 ? args[2] : env.class_name;
    if (!wb::is_identifier(cls)) return "not a valid class name: " + cls;
    const wb::Pattern* pattern = find_pattern(env.catalog, args[1]);
    if (!pattern) return "unknown pattern: " + args[1] + " (see /patterns)";

    const wb::GenerateResult r = wb::generate(env.verifier, *pattern, {{"Name", cls}});
    if (r.ok) {
        return "Verified " + pattern->name + " as " + cls + ":\n```java\n" + r.source + "\n```";
    }
    std::string out = "Rejected " + pattern->name + ":";
    for (const auto& why : r.rejections) out += "\n  " + why;
    return out;
}

std::string cmd_check(CommandEnv& env) {
    if (!env.doc_text) return "No file is open. Open a Java file first.";
    const wb::Analysis a = env.verifier.analyze(*env.doc_text);
    if (a.ok) return "No syntax errors.";
    if (a.errors.empty()) return "Syntax check failed.";
    std::string out = std::to_string(a.errors.size()) + " syntax error(s):";
    for (std::size_t i = 0; i < a.errors.size() && i < 10; ++i) {
        out += "\n  line " + std::to_string(a.errors[i].line) + ": " + a.errors[i].node_type;
    }
    return out;
}

std::string cmd_name(const std::vector<std::string>& args, CommandEnv& env) {
    if (args.size() != 2) return "usage: /name <ClassName>";
    if (!wb::is_identifier(args[1])) return "not a valid class name: " + args[1];
    env.class_name = args[1];
    return "Class name set to " + args[1] + ".";
}

}  // namespace

std::vector<std::string> split_command(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string word;
    while (in >> word) out.push_back(word);
    return out;
}

std::string run_command(const std::string& line, CommandEnv& env) {
    const std::vector<std::string> args = split_command(line);
    if (args.empty()) return help_text();
    std::string name = args[0];
    if (!name.empty() && name[0] == '/') name.erase(0, 1);
    name = lower(name);

    if (name == "help") return help_text();
    if (name == "patterns") return cmd_patterns(env.catalog);
    if (name == "generate") return cmd_generate(args, env);
    if (name == "check") return cmd_check(env);
    if (name == "name") return cmd_name(args, env);
    return "unknown command: " + args[0] + "\n" + help_text();
}

std::string system_prompt() {
    return "You are the assistant inside the Workbench, a desktop tool for Java design patterns. "
           "Answer briefly and concretely. When asked for code, reply with one complete Java file "
           "in a ```java block, using the package and class names the user gave.";
}

std::string build_user_message(const std::vector<Turn>& history, const std::string& question,
                               std::size_t budget) {
    // Walk back from the newest turn and keep whole turns while they fit the budget.
    std::vector<std::string> kept;
    std::size_t used = 0;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->error) continue;
        const std::string line = (it->from_user ? "User: " : "Assistant: ") + it->text;
        if (used + line.size() > budget) break;
        used += line.size();
        kept.push_back(line);
    }
    std::string out;
    if (!kept.empty()) {
        out = "Earlier in this conversation:\n";
        for (auto it = kept.rbegin(); it != kept.rend(); ++it) out += *it + "\n";
        out += "\n";
    }
    return out + "Question: " + question;
}

ChatState::~ChatState() {
    if (worker_.joinable()) worker_.join();
}

void ChatState::send(const std::string& line, CommandEnv& env) {
    if (busy_) return;
    std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) return;
    const std::string text = line.substr(first);

    if (text == "/clear") {
        turns.clear();
        return;
    }

    if (text[0] == '/') {
        turns.push_back({true, false, text});
        std::string reply;
        bool failed = false;
        try {
            reply = run_command(text, env);
        } catch (const std::exception& e) {
            reply = e.what();
            failed = true;
        }
        turns.push_back({false, failed, reply});
        return;
    }

    if (!responder) {
        turns.push_back({true, false, text});
        turns.push_back({false, true,
                         "No model loaded, so I can only run commands. Set WB_MODEL or see the "
                         "README. Type /help for the list."});
        return;
    }

    // The history is taken before this question is added, so it is not sent twice.
    const std::string user = build_user_message(turns, text, kHistoryBudget);
    turns.push_back({true, false, text});
    busy_ = true;
    done_ = false;
    worker_ = std::thread([this, responder = responder, system = system_prompt(), user] {
        Turn t;
        t.from_user = false;
        try {
            t.text = responder(system, user);
        } catch (const std::exception& e) {
            t.error = true;
            t.text = e.what();
        }
        std::lock_guard<std::mutex> lock(mu_);
        pending_ = std::move(t);
        done_ = true;
    });
}

void ChatState::poll() {
    if (!busy_) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!done_) return;
        turns.push_back(std::move(pending_));
        pending_ = Turn{};
        done_ = false;
    }
    worker_.join();
    busy_ = false;
}

}  // namespace app::chat
