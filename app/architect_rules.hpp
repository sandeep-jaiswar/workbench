#pragma once

// Deterministic architecture checks over a project's .java files: a dependency graph between
// top-level types, and rule findings with file and line. No model, no ImGui.
//
// Input is the wb::Session analysis of each file (symbols, imports, package). The graph is
// built from declared type texts (fields, return types, parameters) and imports, matched by
// simple name against the project's own top-level types.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "symbols.hpp"

namespace app::architect_rules {

// A class (not interface) with more methods than this gets a large_class warning.
inline constexpr std::size_t kLargeClassMethods = 30;

// Rules: package_cycle (warning), missing_tests (info), public_mutable_field (warning),
// large_class (warning).
struct Finding {
    std::string rule;
    std::string severity;  // "info" or "warning"
    std::string file;      // path relative to src/, '/' separated
    std::uint32_t line = 0;
    std::string message;

    bool operator==(const Finding& o) const;
};

// Directed edge between top-level types, by qualified name ("pkg.Type", or "Type" when the
// package is empty). Deduplicated; no self-edges.
struct Edge {
    std::string from;
    std::string to;

    bool operator==(const Edge& o) const;
};

struct Architecture {
    std::vector<Finding> findings;  // sorted by rule, file, line, message
    std::vector<Edge> edges;        // sorted by from, to
    std::size_t files = 0;
    std::size_t types = 0;    // all type declarations, nested included
    std::size_t methods = 0;  // all methods, nested included
};

// One parsed file. `path` is relative to the project's src directory.
struct FileReport {
    std::string path;
    wb::Report report;
};

// One source text, for analyze_sources.
struct SourceText {
    std::string path;
    std::string text;
};

// Pure. `test_names` are the stems of the .java files under the project's test folder; a type
// T is covered when "TTest" is in the list. Ambiguous simple names (two top-level types with
// the same name in the project) produce no edges.
Architecture analyze(const std::vector<FileReport>& files, const std::vector<std::string>& test_names);

// Parses each source with wb::Session (same caps as the outline: 2,000 files, 4 MiB each,
// oversized files are skipped), then calls analyze.
Architecture analyze_sources(const std::vector<SourceText>& sources,
                             const std::vector<std::string>& test_names);

// Deterministic text: a summary line, then "findings:" and "edges:" sections. Same input,
// same bytes.
std::string render_text(const Architecture& arch);

}  // namespace app::architect_rules
