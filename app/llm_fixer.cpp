#include "llm_fixer.hpp"

#include <stdexcept>

#include "llm.hpp"

namespace app::fixer {

namespace {

std::string trim_trailing(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    return s;
}

}  // namespace

Prompt build_prompt(const FailureContext& ctx) {
    Prompt p;
    p.system =
        "You are an expert Java developer. The user gives you a failing class and the test report. "
        "Reply with the complete corrected Java file in one ```java code block. After the block, "
        "write one line starting with 'Why:' that explains the change in one sentence.";
    p.user = "File: " + ctx.class_path + "\n\nTest report:\n" + ctx.test_output +
             "\n\nCurrent file:\n```java\n" + ctx.class_source + "\n```\n";
    if (!ctx.test_source.empty())
        p.user += "\nFailing test class (its expectations are the spec):\n```java\n" +
                  ctx.test_source + "\n```\n";
    if (!ctx.feedback.empty())
        p.user += "\nYour previous reply was rejected:\n" + ctx.feedback +
                  "\nFix that and reply with the complete file again.\n";
    return p;
}

std::string extract_java(const std::string& reply) {
    const std::size_t open = reply.find("```");
    if (open == std::string::npos) return {};
    const std::size_t body = reply.find('\n', open);
    if (body == std::string::npos) return {};
    const std::size_t close = reply.find("```", body + 1);
    if (close == std::string::npos) return {};
    return trim_trailing(reply.substr(body + 1, close - body - 1)) + "\n";
}

std::string extract_rationale(const std::string& reply) {
    const std::size_t at = reply.find("Why:");
    if (at == std::string::npos) return "model proposal";
    std::size_t end = reply.find('\n', at);
    std::string text = reply.substr(at + 4, end == std::string::npos ? std::string::npos : end - at - 4);
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    text = trim_trailing(text);
    return text.empty() ? std::string("model proposal") : text;
}

Fixer make_llm_fixer(std::shared_ptr<wb::llm::Model> model) {
    return [model](const FailureContext& ctx) -> Proposal {
        if (!model) throw std::runtime_error("no model loaded");
        if (ctx.class_path.empty()) throw std::runtime_error("no failing class in the project");
        if (ctx.class_source.empty()) throw std::runtime_error("failing class is empty");

        const Prompt prompt = build_prompt(ctx);
        std::string reply;
        std::string error;
        if (!model->chat(prompt.system, prompt.user, &reply, &error))
            throw std::runtime_error("model: " + error);

        const std::string code = extract_java(reply);
        if (code.empty()) throw std::runtime_error("reply has no complete java code block");

        Proposal proposal;
        proposal.class_path = ctx.class_path;
        proposal.new_source = code;
        proposal.rationale = extract_rationale(reply);
        proposal.base_source = ctx.class_source;
        return proposal;
    };
}

}  // namespace app::fixer
