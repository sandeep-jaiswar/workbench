#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "catalog.hpp"
#include "verify.hpp"

namespace wb {

using Params = std::map<std::string, std::string>;

// Optional neural sampler: returns candidate Java sources for the given template.
// Candidates go through the same verifier as the templates.
using Sampler = std::function<std::vector<std::string>(const Pattern&, const Params&,
                                                       const std::string& templ)>;

struct GenerateResult {
    bool ok = false;
    std::string source;                    // set when ok
    std::vector<std::string> rejections;   // one entry per rejected candidate
};

// Valid Java identifier: [A-Za-z_][A-Za-z0-9_]*
bool is_identifier(std::string_view s);

// Replaces {{Key}} placeholders in one pass. Throws std::invalid_argument on a missing key.
std::string fill(std::string_view templ, const Params& params);

// Fills the template, optionally samples alternatives, and returns the first candidate that
// passes verification (best-of-N with early exit). Throws on invalid params.
GenerateResult generate(Verifier& verifier, const Pattern& pattern, const Params& params,
                        const Sampler& sampler = nullptr);

}  // namespace wb
