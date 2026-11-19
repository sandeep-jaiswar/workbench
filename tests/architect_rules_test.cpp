// Architecture graph and rules on a small fixture: no ImGui, no filesystem.
#include <cstdio>
#include <string>
#include <vector>

#include "architect_rules.hpp"

namespace {

using app::architect_rules::Architecture;
using app::architect_rules::Edge;
using app::architect_rules::Finding;
using app::architect_rules::SourceText;

int failures = 0;
int passed = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) {                                                              \
            ++passed;                                                            \
        } else {                                                                 \
            ++failures;                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

// Two packages in a cycle (model <-> service), a public mutable field, a 31-method class and
// types with no test. Orphan.java holds two top-level types; only Holder references Item.
// Line numbers below are 1-based.
std::vector<SourceText> fixture() {
    std::string big = "package app.util;\npublic class Big {\n";  // class on line 2
    for (int i = 0; i < 31; ++i) big += "    void m" + std::to_string(i) + "() {}\n";
    big += "}\n";
    return {
        {"app/model/Item.java",
         "package app.model;\n"
         "import app.service.Pricer;\n"
         "public class Item {\n"
         "    public int qty;\n"
         "    private Pricer pricer;\n"
         "}\n"},
        {"app/service/Pricer.java",
         "package app.service;\n"
         "import app.model.Item;\n"
         "public class Pricer {\n"
         "    public static final int RATE = 2;\n"
         "    public Item price(Item item) { return item; }\n"
         "}\n"},
        {"app/util/Big.java", big},
        {"app/util/Orphan.java",
         "package app.util;\n"
         "class Orphan {\n"
         "    int count;\n"
         "}\n"
         "class Holder {\n"
         "    app.model.Item item;\n"
         "}\n"},
    };
}

const std::vector<std::string> kTests = {"ItemTest", "PricerTest", "BigTest"};

void test_findings_exact() {
    const Architecture arch = app::architect_rules::analyze_sources(fixture(), kTests);
    CHECK(arch.files == 4);
    CHECK(arch.types == 5);
    CHECK(arch.methods == 32);  // 31 in Big, price in Pricer

    const std::vector<Finding> expected = {
        {"large_class", "warning", "app/util/Big.java", 2, "class Big has 31 methods (limit 30)"},
        {"missing_tests", "info", "app/util/Orphan.java", 2, "no test class OrphanTest"},
        {"missing_tests", "info", "app/util/Orphan.java", 5, "no test class HolderTest"},
        {"package_cycle", "warning", "app/model/Item.java", 3,
         "packages form a cycle: app.model, app.service"},
        {"public_mutable_field", "warning", "app/model/Item.java", 4, "public non-final field 'qty' in Item"},
    };
    CHECK(arch.findings == expected);
}

void test_graph_edges() {
    const Architecture arch = app::architect_rules::analyze_sources(fixture(), kTests);
    // Item -> Pricer (field type and import), Pricer -> Item (parameter, return, import),
    // Holder -> Item (qualified field type). Orphan, its sibling in the same file, references
    // nothing, and Pricer's constant and Big reference nothing.
    const std::vector<Edge> expected = {
        {"app.model.Item", "app.service.Pricer"},
        {"app.service.Pricer", "app.model.Item"},
        {"app.util.Holder", "app.model.Item"},
    };
    CHECK(arch.edges == expected);
}

void test_render_is_stable() {
    const Architecture arch = app::architect_rules::analyze_sources(fixture(), kTests);
    const std::string text = app::architect_rules::render_text(arch);
    CHECK(text == app::architect_rules::render_text(arch));
    CHECK(text.rfind("architecture: 4 files, 5 types, 32 methods, 3 edges, 5 findings\n", 0) == 0);
    CHECK(text.find("  warning package_cycle app/model/Item.java:3 packages form a cycle: app.model, app.service\n") !=
          std::string::npos);
    CHECK(text.find("  app.util.Holder -> app.model.Item\n") != std::string::npos);
    CHECK(text.find("Orphan -> ") == std::string::npos);
}

void test_no_findings_when_covered() {
    const Architecture arch = app::architect_rules::analyze_sources(
        {{"a/A.java", "package a;\nclass A { }\n"}}, {"ATest"});
    CHECK(arch.findings.empty());
    CHECK(arch.edges.empty());
    CHECK(app::architect_rules::render_text(arch).find("findings:\n  none\n") != std::string::npos);
}

void test_ambiguous_names_make_no_edges() {
    // Two project types named Shared in different packages: a reference to Shared is ambiguous.
    const Architecture arch = app::architect_rules::analyze_sources(
        {{"p/Shared.java", "package p;\nclass Shared { }\n"},
         {"q/Shared.java", "package q;\nclass Shared { }\n"},
         {"r/User.java", "package r;\nclass User { Shared s; }\n"}},
        {"SharedTest", "UserTest"});
    CHECK(arch.edges.empty());
}

}  // namespace

int main() {
    test_findings_exact();
    test_graph_edges();
    test_render_is_stable();
    test_no_findings_when_covered();
    test_ambiguous_names_make_no_edges();

    std::printf("architect_rules_test: %d passed, %d failed\n", passed, failures);
    return failures == 0 ? 0 : 1;
}
