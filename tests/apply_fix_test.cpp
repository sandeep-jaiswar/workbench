// Headless tests for fix proposals: class-path mapping, the Myers unified diff, and the
// stage/apply protocol. Each case builds a tiny project in a temp directory (not the sample).
// Needs javac and java on PATH. No ImGui, GLFW or display.
// CHECK is used instead of assert because Release builds define NDEBUG.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include "apply_fix.hpp"
#include "fixer.hpp"

namespace fs = std::filesystem;
using namespace app;

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

// A project whose runner passes only when Calc.value() is 42.
const char* kCalcOld =
    "package demo;\n\npublic final class Calc {\n    public static int value() { return 41; }\n}\n";
const char* kCalcFixed =
    "package demo;\n\npublic final class Calc {\n    public static int value() { return 42; }\n}\n";
const char* kRunner =
    "package demo;\n\npublic final class Runner {\n"
    "    public static void main(String[] args) {\n"
    "        if (Calc.value() != 42) {\n"
    "            System.out.println(\"FAIL demo.CalcTest.value: expected 42\");\n"
    "            System.exit(1);\n"
    "        }\n"
    "        System.out.println(\"PASS demo.CalcTest.value\");\n"
    "        System.out.println(\"1 passed, 0 failed\");\n"
    "    }\n}\n";

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

void write_file(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

// Creates a fresh project under the temp directory and removes it on destruction.
struct Project {
    fs::path root;
    Project() {
        std::random_device rd;
        root = fs::temp_directory_path() / ("wb-apply-fix-test-" + std::to_string(rd()));
        write_file(root / "src/demo/Calc.java", kCalcOld);
        write_file(root / "test/demo/Runner.java", kRunner);
    }
    ~Project() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    std::string dir() const { return root.string(); }
    fs::path calc() const { return root / "src/demo/Calc.java"; }
};

fixer::Proposal proposal(const std::string& new_source) {
    fixer::Proposal p;
    p.edits.push_back({"src/demo/Calc.java", new_source, kCalcOld});
    p.rationale = "return 42";
    return p;
}

// ---- unified diff -----------------------------------------------------------

void test_diff_exact_hunk() {
    const std::string old_text = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n";
    const std::string new_text = "a\nb\nc\nd\nE\nf\ng\nh\ni\nj\n";
    const std::string expected =
        "--- a/x.txt\n+++ b/x.txt\n@@ -2,7 +2,7 @@\n"
        " b\n c\n d\n-e\n+E\n f\n g\n h\n";
    CHECK(apply_fix::unified_diff(old_text, new_text, "x.txt") == expected);
}

void test_diff_deletion_and_missing_newline() {
    const std::string expected = "--- a/x.txt\n+++ b/x.txt\n@@ -1,2 +1 @@\n-x\n y\n";
    CHECK(apply_fix::unified_diff("x\ny\n", "y\n", "x.txt") == expected);
}

void test_diff_insert_into_empty_file() {
    const std::string expected = "--- a/x.txt\n+++ b/x.txt\n@@ -0,0 +1 @@\n+a\n";
    CHECK(apply_fix::unified_diff("", "a\n", "x.txt") == expected);
}

void test_diff_two_hunks_when_changes_are_far_apart() {
    std::string old_text;
    for (int i = 1; i <= 20; ++i) old_text += "line" + std::to_string(i) + "\n";
    std::string new_text = old_text;
    new_text.replace(new_text.find("line2\n"), 6, "LINE2\n");
    new_text.replace(new_text.find("line18\n"), 7, "LINE18\n");
    const std::string diff = apply_fix::unified_diff(old_text, new_text, "f.txt");
    CHECK(diff.find("@@ -1,5 +1,5 @@") != std::string::npos);
    CHECK(diff.find("@@ -15,6 +15,6 @@") != std::string::npos);
    CHECK(apply_fix::unified_diff(old_text, old_text, "f.txt").empty());
}

// ---- class path mapping -----------------------------------------------------

void test_class_path_for_failure() {
    Project p;
    write_file(p.root / "src/demo/Cart.java", "package demo;\n");
    const std::string out =
        "1 passed, 1 failed\nFAIL demo.CartTest.testTaxRoundsHalfUp: AssertionError: x\n";
    CHECK(fixer::class_path_for_failure(p.dir(), out) == "src/demo/Cart.java");
    CHECK(fixer::class_path_for_failure(p.dir(), "0 passed, 1 failed; FAIL demo.CartTest.a, demo.X.b") ==
          "src/demo/Cart.java");
    // No file for the mapped class: empty.
    CHECK(fixer::class_path_for_failure(p.dir(), "FAIL demo.MissingTest.m: x").empty());
    // Not a Test class, or no method: empty.
    CHECK(fixer::class_path_for_failure(p.dir(), "FAIL demo.Cart.run: x").empty());
    CHECK(fixer::class_path_for_failure(p.dir(), "1 passed, 0 failed").empty());
}

// ---- stage and apply --------------------------------------------------------

void test_stage_green_leaves_project_alone() {
    Project p;
    const apply_fix::StageResult r = apply_fix::stage(p.dir(), proposal(kCalcFixed), "demo.Runner");
    CHECK(r.error.empty());
    CHECK(r.compiled);
    CHECK(r.tested);
    CHECK(r.green());
    CHECK(r.test_output.find("1 passed") != std::string::npos);
    CHECK(read_file(p.calc()) == kCalcOld);
    CHECK(!fs::exists(p.root / "out"));
}

void test_apply_refused_when_not_confirmed() {
    Project p;
    const fixer::Proposal prop = proposal(kCalcFixed);
    const apply_fix::StageResult r = apply_fix::stage(p.dir(), prop, "demo.Runner");
    CHECK(r.green());
    const apply_fix::ApplyResult a = apply_fix::apply(p.dir(), prop, r, false);
    CHECK(!a.written);
    CHECK(!a.error.empty());
    CHECK(read_file(p.calc()) == kCalcOld);
    CHECK(!fs::exists(p.root / "out" / "backups"));
}

void test_apply_refused_when_build_fails() {
    Project p;
    const std::string broken = "package demo;\npublic final class Calc { int value() { return } }\n";
    const fixer::Proposal prop = proposal(broken);
    const apply_fix::StageResult r = apply_fix::stage(p.dir(), prop, "demo.Runner");
    CHECK(r.error.empty());
    CHECK(!r.compiled);
    CHECK(!r.tested);
    const apply_fix::ApplyResult a = apply_fix::apply(p.dir(), prop, r, true);
    CHECK(!a.written);
    CHECK(!a.conflict);
    CHECK(read_file(p.calc()) == kCalcOld);
    CHECK(!fs::exists(p.root / "out" / "backups"));
}

void test_apply_refused_when_tests_fail() {
    Project p;
    const fixer::Proposal prop = proposal(kCalcOld);  // compiles, but the runner still fails
    const apply_fix::StageResult r = apply_fix::stage(p.dir(), prop, "demo.Runner");
    CHECK(r.compiled);
    CHECK(!r.tested);
    const apply_fix::ApplyResult a = apply_fix::apply(p.dir(), prop, r, true);
    CHECK(!a.written);
    CHECK(read_file(p.calc()) == kCalcOld);
}

void test_apply_refused_on_conflict() {
    Project p;
    fixer::Proposal prop = proposal(kCalcFixed);
    const apply_fix::StageResult r = apply_fix::stage(p.dir(), prop, "demo.Runner");
    CHECK(r.green());
    // The proposal was made against other text than the file on disk.
    prop.edits[0].base_source = "package demo;\n// edited elsewhere\n";
    const apply_fix::ApplyResult a = apply_fix::apply(p.dir(), prop, r, true);
    CHECK(!a.written);
    CHECK(a.conflict);
    CHECK(read_file(p.calc()) == kCalcOld);
    CHECK(!fs::exists(p.root / "out" / "backups"));
}

void test_apply_writes_backup_and_new_file() {
    Project p;
    const fixer::Proposal prop = proposal(kCalcFixed);
    const apply_fix::StageResult r = apply_fix::stage(p.dir(), prop, "demo.Runner");
    CHECK(r.green());
    const apply_fix::ApplyResult a = apply_fix::apply(p.dir(), prop, r, true);
    CHECK(a.written);
    CHECK(!a.conflict);
    CHECK(a.error.empty());
    CHECK(read_file(p.calc()) == kCalcFixed);

    const fs::path backup_dir = p.root / "out" / "backups";
    CHECK(fs::is_directory(backup_dir));
    CHECK(!a.backup_path.empty());
    CHECK(fs::path(a.backup_path).parent_path() == backup_dir);
    CHECK(fs::path(a.backup_path).filename().string().size() > std::string("_Calc.java").size());
    CHECK(fs::path(a.backup_path).filename().string().find("_Calc.java") != std::string::npos);
    CHECK(read_file(a.backup_path) == kCalcOld);
}

}  // namespace

// A two-file proposal: when the second file changed on disk, nothing is written, not even the
// first file. When both match, both are written and both have backups.
void test_apply_multi_file_is_all_or_nothing() {
    Project p;
    write_file(p.root / "src/demo/Other.java", "package demo;\nclass Other {}\n");
    fixer::Proposal prop = proposal(kCalcFixed);
    prop.edits.push_back({"src/demo/Other.java", "package demo;\nclass Other { int x; }\n",
                          "package demo;\nclass Other {}\n"});
    apply_fix::StageResult green;
    green.compiled = true;
    green.tested = true;

    // The second file was edited elsewhere after the proposal was made.
    write_file(p.root / "src/demo/Other.java", "package demo;\n// changed\nclass Other {}\n");
    const apply_fix::ApplyResult refused = apply_fix::apply(p.dir(), prop, green, true);
    CHECK(!refused.written);
    CHECK(refused.conflict);
    CHECK(read_file(p.calc()) == kCalcOld);

    // Matching text on both files: both are written, and each has a backup.
    write_file(p.root / "src/demo/Other.java", "package demo;\nclass Other {}\n");
    const apply_fix::ApplyResult ok = apply_fix::apply(p.dir(), prop, green, true);
    CHECK(ok.written);
    CHECK(ok.written_paths.size() == 2);
    CHECK(ok.backups.size() == 2);
    CHECK(read_file(p.calc()) == kCalcFixed);
    CHECK(read_file(p.root / "src/demo/Other.java") == "package demo;\nclass Other { int x; }\n");
}

int main() {
    test_diff_exact_hunk();
    test_diff_deletion_and_missing_newline();
    test_diff_insert_into_empty_file();
    test_diff_two_hunks_when_changes_are_far_apart();
    test_class_path_for_failure();
    test_stage_green_leaves_project_alone();
    test_apply_refused_when_not_confirmed();
    test_apply_refused_when_build_fails();
    test_apply_refused_when_tests_fail();
    test_apply_refused_on_conflict();
    test_apply_writes_backup_and_new_file();
    test_apply_multi_file_is_all_or_nothing();

    if (failures == 0) std::printf("apply_fix_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
