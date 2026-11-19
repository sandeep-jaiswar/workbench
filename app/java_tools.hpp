#pragma once

#include <string>
#include <vector>

// Runs external tools (javac, java) for the editor. Commands run without a shell.
namespace wbx {

struct RunResult {
    int exit_code = -1;       // -1 if the process could not start or was killed
    bool timed_out = false;
    std::string output;       // stdout and stderr combined, capped at 64 KiB
};

// One javac error: "<file>:<line>: error: <message>".
struct Diagnostic {
    std::string file;
    int line = 0;
    std::string message;
};

// Runs argv[0] with argv[1..] and captures output. Kills the child after timeout_seconds.
RunResult run_process(const std::vector<std::string>& argv, double timeout_seconds);

// javac over every .java file under <project>/src into <project>/out/classes.
RunResult compile_project(const std::string& project_dir);

// java -cp out/classes:out/test-classes <main_class> in <project>.
RunResult run_tests(const std::string& project_dir, const std::string& main_class);

// Extracts the javac error lines from captured output. Other lines are ignored.
std::vector<Diagnostic> parse_javac_output(const std::string& output);

// Result lines of the sample test runner: "PASS <name>", "FAIL <name>: <message>" and a final
// "N passed, M failed". `failing` holds the names from FAIL lines, in output order.
struct TestResult {
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failing;
};

// Pure: reads the runner's output. Counts come from the final summary line when there is
// one, otherwise from the PASS and FAIL lines.
TestResult parse_test_output(const std::string& output);

}  // namespace wbx
