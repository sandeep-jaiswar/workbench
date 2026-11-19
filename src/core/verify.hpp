#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

struct TSParser;

namespace wb {

struct SyntaxIssue {
    std::string node_type;  // "ERROR" or the expected token for missing nodes
    std::uint32_t line;     // 1-based
    bool missing;
};

struct Analysis {
    bool ok = false;
    std::vector<SyntaxIssue> errors;
    std::unordered_set<std::string> types;    // O(1) membership
    std::unordered_set<std::string> methods;  // O(1) membership
};

// Owns one tree-sitter parser configured for Java. Reuse it across calls.
class Verifier {
public:
    Verifier();
    ~Verifier();
    Verifier(const Verifier&) = delete;
    Verifier& operator=(const Verifier&) = delete;

    // One iterative pass over the syntax tree: O(n) in source size.
    Analysis analyze(std::string_view source);

private:
    TSParser* parser_ = nullptr;
};

// Returns problems such as "missing method getInstance". Empty means the requirements hold.
std::vector<std::string> check_requirements(const Analysis& analysis,
                                            const std::vector<std::string>& types,
                                            const std::vector<std::string>& methods);

}  // namespace wb
