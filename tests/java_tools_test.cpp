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

}  // namespace

int main() {
    test_echo();
    test_timeout();
    test_missing_binary();
    test_parse_javac();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("java_tools_test: all checks passed\n");
    return 0;
}
