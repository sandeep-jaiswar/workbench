// Headless tests for the "Tests for missing classes" logic. Links wb_core only: no ImGui, GLFW
// or display. The file-system case uses a temp folder that it removes at the end.
// CHECK is used instead of assert because Release builds define NDEBUG.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "session.hpp"
#include "test_gen.hpp"

namespace fs = std::filesystem;
using namespace app::test_gen;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                             \
            ++failures;                                                \
        }                                                              \
    } while (0)

void test_class_names_from_files() {
    const std::vector<std::string> names = test_class_names(
        {"CartTest.java", "demo/MoneyTest.java", "TestRunner.java", "Test.java", "Check.java",
         "ReceiptTest.txt", "Nested/OrderTest"});
    CHECK((names == std::vector<std::string>{"CartTest", "MoneyTest", "OrderTest"}));
}

void test_file_paths() {
    const fs::path root = "/p/test";
    CHECK(test_file_path(root, "demo", "Item") == fs::path("/p/test/demo/ItemTest.java"));
    CHECK(test_file_path(root, "com.acme.shop", "Cart") == fs::path("/p/test/com/acme/shop/CartTest.java"));
    CHECK(test_file_path(root, "", "Main") == fs::path("/p/test/MainTest.java"));
}

const char* kMoney =
    "package demo;\n"
    "public final class Money {\n"
    "    static class Helper { void run() {} }\n"
    "    public long cents() { return 0; }\n"
    "}\n";
const char* kShapes =
    "package demo;\n"
    "interface TaxPolicy { double rate(); }\n"
    "enum Unit { CM, MM }\n"
    "record Item(String name) {}\n";
const char* kMain = "public class Main { public static void main(String[] a) {} }\n";

std::vector<ParsedSource> sample_sources() {
    wb::Session session;
    return {parse_source("demo/Money.java", kMoney, session),
            parse_source("demo/Shapes.java", kShapes, session),
            parse_source("Main.java", kMain, session)};
}

void test_top_level_types() {
    const std::vector<ParsedSource> sources = sample_sources();
    const std::vector<TopLevelType> types = top_level_types(sources);
    // Money, TaxPolicy, Unit, Item, Main. The nested Helper is not top-level.
    std::vector<std::string> names;
    for (const TopLevelType& t : types) names.push_back(t.name);
    CHECK((names == std::vector<std::string>{"Money", "TaxPolicy", "Unit", "Item", "Main"}));
    CHECK(sources[0].package == "demo");
    CHECK(sources[2].package.empty());
}

void test_plan_skips_tested_and_existing() {
    const std::vector<ParsedSource> sources = sample_sources();
    const fs::path test_root = "/proj/test";
    const std::vector<NewTest> plan = plan_missing(
        sources, {"MoneyTest"}, test_root,
        [&](const fs::path& p) { return p == test_root / "demo" / "UnitTest.java"; });

    // Money has a test; Unit exists on disk; the rest need stubs, in source order.
    std::vector<std::string> names;
    for (const NewTest& t : plan) names.push_back(t.type_name);
    CHECK((names == std::vector<std::string>{"TaxPolicy", "Item", "Main"}));
    if (plan.size() == 3) {
        CHECK(plan[0].path == test_root / "demo" / "TaxPolicyTest.java");
        CHECK(plan[2].path == test_root / "MainTest.java");
        CHECK(plan[0].contents.find("class TaxPolicyTest") != std::string::npos);
        CHECK(plan[0].contents.find("package demo;") != std::string::npos);
        CHECK(plan[2].contents.find("class MainTest") != std::string::npos);
    }

    // Nothing missing when every type has a test class.
    const std::vector<NewTest> none = plan_missing(
        sources, {"MoneyTest", "TaxPolicyTest", "UnitTest", "ItemTest", "MainTest"}, test_root, nullptr);
    CHECK(none.empty());
}

void test_duplicate_types_give_one_file() {
    wb::Session session;
    const std::vector<ParsedSource> sources = {
        parse_source("a/Foo.java", "package a;\nclass Foo {}\n", session),
        parse_source("b/Foo.java", "package a;\nclass Foo {}\n", session)};
    const std::vector<NewTest> plan = plan_missing(sources, {}, "/t", nullptr);
    CHECK(plan.size() == 1);
}

void test_test_names_from_disk() {
    // Real files in a temp folder: src/ and test/. Creates stubs, skips existing ones, and a
    // second run finds nothing to do.
    const fs::path base = fs::temp_directory_path() /
                          ("wb_test_gen_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(base / "src" / "demo");
    fs::create_directories(base / "test" / "demo");
    {
        std::ofstream(base / "src" / "demo" / "Money.java") << kMoney;
        std::ofstream(base / "src" / "demo" / "Shapes.java") << kShapes;
        std::ofstream(base / "src" / "Main.java") << kMain;
        std::ofstream(base / "test" / "demo" / "MoneyTest.java") << "package demo;\nclass MoneyTest {}\n";
        // An existing stub with content that must survive.
        std::ofstream(base / "test" / "demo" / "ItemTest.java") << "KEEP ME";
    }

    const Report first = generate_missing_tests(base);
    CHECK(!first.failed);
    CHECK(first.source_files == 3);
    CHECK(first.top_level_types == 5);
    CHECK(first.test_classes == 2);  // MoneyTest and ItemTest
    // TaxPolicy, Unit (no test yet), Main. Item already had a file, so it is kept as is.
    std::set<std::string> created;
    for (const fs::path& p : first.created) created.insert(p.filename().string());
    CHECK((created == std::set<std::string>{"TaxPolicyTest.java", "UnitTest.java", "MainTest.java"}));
    CHECK(fs::exists(base / "test" / "demo" / "TaxPolicyTest.java"));
    CHECK(fs::exists(base / "test" / "MainTest.java"));
    {
        std::ifstream in(base / "test" / "demo" / "ItemTest.java");
        std::string kept((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(kept == "KEEP ME");
    }

    const Report second = generate_missing_tests(base);
    CHECK(second.created.empty());
    CHECK(second.errors.empty());
    CHECK(format_report(second, base).find("Nothing missing.") != std::string::npos);
    CHECK(format_report(first, base).find("Created 3 file(s):") != std::string::npos);

    // No src folder: reported as a failure, nothing created.
    const Report none = generate_missing_tests(base / "missing");
    CHECK(none.failed);
    CHECK(none.created.empty());

    std::error_code ec;
    fs::remove_all(base, ec);
    CHECK(!fs::exists(base));
}

}  // namespace

int main() {
    test_class_names_from_files();
    test_file_paths();
    test_top_level_types();
    test_plan_skips_tested_and_existing();
    test_duplicate_types_give_one_file();
    test_test_names_from_disk();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_gen_test: all checks passed\n");
    return 0;
}
