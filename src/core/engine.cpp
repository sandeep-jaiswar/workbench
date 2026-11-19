#include "engine.hpp"

#include <cctype>
#include <stdexcept>

namespace wb {

bool is_identifier(std::string_view s) {
    if (s.empty()) return false;
    const auto first = static_cast<unsigned char>(s[0]);
    if (!(std::isalpha(first) || s[0] == '_')) return false;
    for (char c : s) {
        const auto uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_')) return false;
    }
    return true;
}

std::string fill(std::string_view templ, const Params& params) {
    std::string out;
    out.reserve(templ.size());
    std::size_t pos = 0;
    while (pos < templ.size()) {
        const auto open = templ.find("{{", pos);
        if (open == std::string_view::npos) {
            out.append(templ.substr(pos));
            break;
        }
        const auto close = templ.find("}}", open + 2);
        if (close == std::string_view::npos) {
            out.append(templ.substr(pos));
            break;
        }
        out.append(templ.substr(pos, open - pos));
        const std::string key(templ.substr(open + 2, close - open - 2));
        const auto it = params.find(key);
        if (it == params.end()) throw std::invalid_argument("missing param " + key);
        out.append(it->second);
        pos = close + 2;
    }
    return out;
}

GenerateResult generate(Verifier& verifier, const Pattern& pattern, const Params& params,
                        const Sampler& sampler) {
    for (const auto& p : pattern.params) {
        const auto it = params.find(p);
        if (it == params.end() || !is_identifier(it->second)) {
            throw std::invalid_argument("invalid identifier for " + p);
        }
    }

    const std::string deterministic = fill(pattern.templ, params);
    std::vector<std::string> candidates;
    if (sampler) {
        candidates = sampler(pattern, params, deterministic);
    } else {
        candidates.push_back(deterministic);
    }

    std::vector<std::string> required_types;
    for (const auto& t : pattern.requires.types) required_types.push_back(fill(t, params));

    GenerateResult result;
    for (const auto& source : candidates) {
        const Analysis analysis = verifier.analyze(source);
        if (!analysis.ok) {
            result.rejections.push_back("syntax: line " +
                                        std::to_string(analysis.errors.front().line));
            continue;
        }
        const auto problems =
            check_requirements(analysis, required_types, pattern.requires.methods);
        if (!problems.empty()) {
            std::string joined;
            for (const auto& p : problems) joined += (joined.empty() ? "" : "; ") + p;
            result.rejections.push_back("structure: " + joined);
            continue;
        }
        result.ok = true;
        result.source = source;
        return result;
    }
    return result;
}

}  // namespace wb
