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

// The fenced ```java block that starts at or after `from`. Sets *next to where it ends.
bool next_block(const std::string& reply, std::size_t from, std::string* code, std::size_t* next) {
    const std::size_t open = reply.find("```", from);
    if (open == std::string::npos) return false;
    const std::size_t body = reply.find('\n', open);
    if (body == std::string::npos) return false;
    const std::size_t close = reply.find("```", body + 1);
    if (close == std::string::npos) return false;
    *code = trim_trailing(reply.substr(body + 1, close - body - 1)) + "\n";
    *next = close + 3;
    return true;
}

}  // namespace

Prompt build_prompt(const FailureContext& ctx) {
    Prompt p;
    p.system =
        "You are an expert Java developer. You fix a failing test by changing the project files. "
        "For every file you change, write a line 'File: <path>' followed by the complete corrected "
        "file in one ```java code block. Only change files that need it. After the blocks, write one "
        "line starting with 'Why:' that explains the change in one sentence.";
    p.user = "Failing class: " + ctx.class_path + "\n\nTest report:\n" + ctx.test_output + "\n";
    if (!ctx.test_source.empty())
        p.user += "\nFailing test class (its expectations are the spec):\n```java\n" +
                  ctx.test_source + "\n```\n";
    p.user += "\nProject files you may change:\n";
    for (const SourceFile& f : ctx.files)
        p.user += "\nFile: " + f.path + "\n```java\n" + f.text + "\n```\n";
    if (!ctx.feedback.empty())
        p.user += "\nYour previous reply was rejected:\n" + ctx.feedback +
                  "\nFix that and reply again with the changed files.\n";
    return p;
}

std::vector<ParsedFile> parse_files(const std::string& reply, const std::vector<std::string>& allowed) {
    std::vector<ParsedFile> out;
    std::size_t at = 0;
    while ((at = reply.find("File:", at)) != std::string::npos) {
        std::size_t eol = reply.find('\n', at);
        if (eol == std::string::npos) break;
        std::string path = reply.substr(at + 5, eol - at - 5);
        while (!path.empty() && path.front() == ' ') path.erase(path.begin());
        path = trim_trailing(path);
        // Accept "File: `src/x.java`" and similar wrappers around the path.
        std::string bare;
        for (char c : path) if (c != '`' && c != '*') bare.push_back(c);
        path = bare;
        std::string code;
        std::size_t next = 0;
        at = eol;
        if (!next_block(reply, eol, &code, &next)) break;
        at = next;
        bool known = false;
        for (const std::string& a : allowed) known = known || a == path;
        if (known) out.push_back({path, code});
    }
    return out;
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
        if (ctx.files.empty()) throw std::runtime_error("no project files to change");

        const Prompt prompt = build_prompt(ctx);
        std::string reply;
        std::string error;
        if (!model->chat(prompt.system, prompt.user, &reply, &error))
            throw std::runtime_error("model: " + error);

        std::vector<std::string> allowed;
        for (const SourceFile& f : ctx.files) allowed.push_back(f.path);

        Proposal proposal;
        proposal.rationale = extract_rationale(reply);
        for (const ParsedFile& pf : parse_files(reply, allowed)) {
            for (const SourceFile& f : ctx.files) {
                if (f.path != pf.path) continue;
                proposal.edits.push_back({f.path, pf.code, f.text});
                break;
            }
        }
        return proposal;
    };
}

}  // namespace app::fixer
