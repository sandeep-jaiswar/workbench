#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace wb {

struct Requirements {
    std::vector<std::string> types;    // may contain {{Param}} placeholders
    std::vector<std::string> methods;  // literal names
};

struct Pattern {
    std::string id;
    std::string name;
    std::string intent;
    std::vector<std::string> params;   // e.g. {"Name"}
    std::string templ;                 // Java source with {{Param}} placeholders
    Requirements requires;
};

class Catalog {
public:
    // Loads the JSON catalog. Throws std::runtime_error on failure.
    static Catalog load(const std::string& path);

    const std::vector<Pattern>& patterns() const { return patterns_; }
    // O(1) lookup by id; returns nullptr when unknown.
    const Pattern* find(const std::string& id) const;

private:
    std::vector<Pattern> patterns_;
    std::unordered_map<std::string, std::size_t> index_;
};

}  // namespace wb
