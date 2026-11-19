#include "verify.hpp"

#include <cstring>
#include <stdexcept>

#include <tree_sitter/api.h>

extern "C" const TSLanguage* tree_sitter_java(void);

namespace wb {
namespace {

bool is_type_declaration(const char* type) {
    return std::strcmp(type, "class_declaration") == 0 ||
           std::strcmp(type, "interface_declaration") == 0 ||
           std::strcmp(type, "enum_declaration") == 0 ||
           std::strcmp(type, "record_declaration") == 0;
}

std::string node_text(TSNode node, std::string_view source) {
    const auto start = ts_node_start_byte(node);
    const auto end = ts_node_end_byte(node);
    return std::string(source.substr(start, end - start));
}

}  // namespace

Verifier::Verifier() {
    parser_ = ts_parser_new();
    if (!ts_parser_set_language(parser_, tree_sitter_java())) {
        ts_parser_delete(parser_);
        throw std::runtime_error("tree-sitter Java language ABI mismatch");
    }
}

Verifier::~Verifier() {
    ts_parser_delete(parser_);
}

Analysis Verifier::analyze(std::string_view source) {
    Analysis result;
    TSTree* tree = ts_parser_parse_string(parser_, nullptr, source.data(),
                                          static_cast<std::uint32_t>(source.size()));
    if (!tree) return result;

    // Explicit stack instead of recursion: no depth limit on deeply nested input.
    std::vector<TSNode> stack{ts_tree_root_node(tree)};
    while (!stack.empty()) {
        const TSNode node = stack.back();
        stack.pop_back();
        const char* type = ts_node_type(node);
        const bool missing = ts_node_is_missing(node);

        if (missing || std::strcmp(type, "ERROR") == 0) {
            result.errors.push_back({type, ts_node_start_point(node).row + 1, missing});
        }

        if (is_type_declaration(type)) {
            const TSNode name = ts_node_child_by_field_name(node, "name", 4);
            if (!ts_node_is_null(name)) result.types.insert(node_text(name, source));
        } else if (std::strcmp(type, "method_declaration") == 0) {
            const TSNode name = ts_node_child_by_field_name(node, "name", 4);
            if (!ts_node_is_null(name)) result.methods.insert(node_text(name, source));
        }

        // Push children in reverse so they pop in source order.
        for (std::uint32_t i = ts_node_child_count(node); i-- > 0;) {
            stack.push_back(ts_node_child(node, i));
        }
    }
    ts_tree_delete(tree);

    result.ok = result.errors.empty();
    return result;
}

std::vector<std::string> check_requirements(const Analysis& analysis,
                                            const std::vector<std::string>& types,
                                            const std::vector<std::string>& methods) {
    std::vector<std::string> problems;
    for (const auto& t : types) {
        if (!analysis.types.count(t)) problems.push_back("missing type " + t);
    }
    for (const auto& m : methods) {
        if (!analysis.methods.count(m)) problems.push_back("missing method " + m);
    }
    return problems;
}

}  // namespace wb
