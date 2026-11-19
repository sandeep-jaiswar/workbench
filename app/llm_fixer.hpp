#pragma once

// A Fixer backed by the local llama.cpp model (wb_llm). The prompt building and reply parsing
// are pure and tested without a model. The model returns whole files; the proposal still goes
// through the staged, confirmed apply, so a bad reply cannot change the project by itself.

#include <memory>
#include <string>
#include <vector>

#include "fixer.hpp"

namespace wb::llm {
class Model;
}

namespace app::fixer {

struct Prompt {
    std::string system;
    std::string user;
};

Prompt build_prompt(const FailureContext& ctx);

// One changed file from a reply: the path named in its "File:" line and the code in the block
// that follows.
struct ParsedFile {
    std::string path;
    std::string code;
};

// Every "File: <path>" line followed by a complete ```java block, in reply order. A block whose
// path is not in `allowed` is dropped. Empty when nothing usable is present.
std::vector<ParsedFile> parse_files(const std::string& reply, const std::vector<std::string>& allowed);

// The text after a "Why:" line, or "model proposal" when there is none.
std::string extract_rationale(const std::string& reply);

// Throws from the returned fixer only when the model itself fails. A reply with no usable file
// block returns a proposal with no edits, which the debug step rejects with feedback.
Fixer make_llm_fixer(std::shared_ptr<wb::llm::Model> model);

}  // namespace app::fixer
