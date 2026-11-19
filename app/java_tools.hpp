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

// Runs argv[0] with argv[1..] and captures output. Kills the child after timeout_seconds.
RunResult run_process(const std::vector<std::string>& argv, double timeout_seconds);

// javac over every .java file under <project>/src into <project>/out/classes.
RunResult compile_project(const std::string& project_dir);

// java -cp out/classes:out/test-classes <main_class> in <project>.
RunResult run_tests(const std::string& project_dir, const std::string& main_class);

}  // namespace wbx
