#pragma once

// A Fixer backed by the local llama.cpp model (wb_llm). The prompt building and reply parsing
// are pure and tested without a model. The model returns a whole file; the proposal still goes
// through the staged, confirmed apply, so a bad reply cannot change the project by itself.

#include <memory>
#include <string>

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

// The first fenced code block of `reply` (```java or bare ```), without the fences. Empty when
// there is no complete block.
std::string extract_java(const std::string& reply);

// The text after a "Why:" line, or "model proposal" when there is none.
std::string extract_rationale(const std::string& reply);

// Throws from the returned fixer when the model fails, the reply has no code block, the
// context names no class, or the reply leaves the class unchanged.
Fixer make_llm_fixer(std::shared_ptr<wb::llm::Model> model);

}  // namespace app::fixer
