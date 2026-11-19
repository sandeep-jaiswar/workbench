// Headless tests for wbx::run_process and javac output parsing. No display, ImGui or GLFW.
// CHECK is used instead of assert because Release builds define NDEBUG.
#include <cstdio>
#include <string>

#include "java_tools.hpp"

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

void test_echo() {
    const wbx::RunResult r = wbx::run_process({"/bin/echo", "hi"}, 10.0);
    CHECK(r.output == "hi\n");
    CHECK(r.exit_code == 0);
    CHECK(!r.timed_out);
}

void test_timeout() {
    const wbx::RunResult r = wbx::run_process({"/bin/sleep", "5"}, 0.3);
    CHECK(r.timed_out);
    CHECK(r.exit_code == -1);
}

void test_missing_binary() {
    const wbx::RunResult r = wbx::run_process({"/no/such/binary-workbench"}, 10.0);
    CHECK(r.exit_code == -1);
    CHECK(!r.timed_out);
}

void test_parse_javac() {
    const std::string out =
        "/work/src/demo/Cart.java:42: error: ';' expected\n"
        "    int x = 1\n"
        "            ^\n"
        "1 error\n"
        "warning: [options] something\n"
        "src/demo/Money.java:7: error: cannot find symbol\n"
        "not an error line: 9\n";
    const std::vector<wbx::Diagnostic> d = wbx::parse_javac_output(out);
    CHECK(d.size() == 2);
    if (d.size() == 2) {
        CHECK(d[0].file == "/work/src/demo/Cart.java");
        CHECK(d[0].line == 42);
        CHECK(d[0].message == "';' expected");
        CHECK(d[1].file == "src/demo/Money.java");
        CHECK(d[1].line == 7);
        CHECK(d[1].message == "cannot find symbol");
    }
}

// Real javac 25 output (--release 17) for a scratch file with two errors on line 6, the
// "return x" line that follows "int x = a +" (missing ';' and an illegal start of expression).
void test_parse_javac_caret_column() {
    const std::string out =
        "src/demo/Broken.java:6: error: illegal start of expression\n"
        "        return x\n"
        "        ^\n"
        "src/demo/Broken.java:6: error: ';' expected\n"
        "        return x\n"
        "                ^\n"
        "2 errors\n";
    const std::vector<wbx::Diagnostic> d = wbx::parse_javac_output(out);
    CHECK(d.size() == 2);
    if (d.size() == 2) {
        CHECK(d[0].line == 6);
        CHECK(d[0].column == 9);  // '^' at index 8: eight spaces, under "return"
        CHECK(d[0].message == "illegal start of expression");
        CHECK(d[1].line == 6);
        CHECK(d[1].column == 17);  // '^' at index 16: sixteen spaces, under the ';' position
        CHECK(d[1].message == "';' expected");
    }
}

void test_parse_javac_caret_tabs_and_missing_caret() {
    // Tabs in the source line are kept in the caret line, so the index still counts characters.
    const std::string tabbed =
        "/w/A.java:3: error: not a statement\n"
        "\tint y = 2 3;\n"
        "\t\t    ^\n";
    const std::vector<wbx::Diagnostic> t = wbx::parse_javac_output(tabbed);
    CHECK(t.size() == 1);
    if (t.size() == 1) CHECK(t[0].column == 7);  // two tabs and four spaces: '^' at index 6

    // No caret line (javac without a source line, or a line that is not a caret): column 1.
    const std::string bare =
        "/w/B.java:9: error: class, interface, enum, or record expected\n"
        "1 error\n";
    const std::vector<wbx::Diagnostic> b = wbx::parse_javac_output(bare);
    CHECK(b.size() == 1);
    if (b.size() == 1) CHECK(b[0].column == 1);

    // A source line that contains '^' is not a caret line: it has code before the '^'.
    const std::string operator_line =
        "/w/C.java:2: error: bad operand\n"
        "    x = a ^ b;\n"
        "          ^\n";
    const std::vector<wbx::Diagnostic> c = wbx::parse_javac_output(operator_line);
    CHECK(c.size() == 1);
    if (c.size() == 1) CHECK(c[0].column == 11);
}

}  // namespace

int main() {
    test_echo();
    test_timeout();
    test_missing_binary();
    test_parse_javac();
    test_parse_javac_caret_column();
    test_parse_javac_caret_tabs_and_missing_caret();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("java_tools_test: all checks passed\n");
    return 0;
}
