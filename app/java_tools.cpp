#include "java_tools.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace wbx {
namespace {

constexpr std::size_t kMaxOutput = 64 * 1024;
constexpr double kCompileTimeout = 60.0;
constexpr double kTestTimeout = 60.0;

// Starts argv in `cwd` (empty means the current directory) with stdout and stderr
// on one pipe. Returns false and sets `error` if exec fails.
struct Child {
    pid_t pid = -1;
    int out_fd = -1;
};

bool spawn(const std::vector<std::string>& argv, const std::string& cwd, Child& child,
           std::string& error) {
    if (argv.empty()) {
        error = "empty command";
        return false;
    }
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    int out_pipe[2];
    int err_pipe[2];
    if (pipe2(out_pipe, O_CLOEXEC) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        return false;
    }
    if (pipe2(err_pipe, O_CLOEXEC) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        return false;
    }

    // Only async-signal-safe calls run in the child before exec.
    const pid_t pid = fork();
    if (pid < 0) {
        error = std::string("fork: ") + std::strerror(errno);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        return false;
    }
    if (pid == 0) {
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
            const int e = errno;
            (void)!write(err_pipe[1], &e, sizeof e);
            _exit(127);
        }
        execvp(args[0], args.data());
        const int e = errno;
        (void)!write(err_pipe[1], &e, sizeof e);
        _exit(127);
    }

    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    int exec_errno = 0;
    ssize_t n;
    do {
        n = read(err_pipe[0], &exec_errno, sizeof exec_errno);
    } while (n < 0 && errno == EINTR);
    ::close(err_pipe[0]);

    if (n == static_cast<ssize_t>(sizeof exec_errno)) {  // the child reported a failure
        ::close(out_pipe[0]);
        int status = 0;
        waitpid(pid, &status, 0);  // reap; never leave a zombie
        error = std::string("cannot start ") + argv[0] + ": " + std::strerror(exec_errno);
        return false;
    }
    child.pid = pid;
    child.out_fd = out_pipe[0];
    return true;
}

// Appends what `fd` has now, without blocking. Returns false at end of file.
bool drain(int fd, std::string& out, bool& truncated) {
    char buf[4096];
    for (;;) {
        const ssize_t n = read(fd, buf, sizeof buf);
        if (n > 0) {
            const std::size_t room = kMaxOutput > out.size() ? kMaxOutput - out.size() : 0;
            if (room > 0) out.append(buf, std::min(room, static_cast<std::size_t>(n)));
            if (static_cast<std::size_t>(n) > room) truncated = true;
            continue;
        }
        if (n == 0) return false;
        if (errno == EINTR) continue;
        return errno == EAGAIN;  // true: nothing more right now
    }
}

RunResult run_in(const std::vector<std::string>& argv, const std::string& cwd, double timeout_seconds) {
    RunResult result;
    Child child;
    std::string error;
    if (!spawn(argv, cwd, child, error)) {
        result.output = error + "\n";
        return result;
    }

    // Non-blocking reads in short slices. The child is reaped as soon as it exits, and
    // killed when the deadline passes, whether or not its output has ended.
    const int fl = fcntl(child.out_fd, F_GETFL, 0);
    fcntl(child.out_fd, F_SETFL, fl | O_NONBLOCK);

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<double>(timeout_seconds));
    bool truncated = false;
    bool reaped = false;
    int status = 0;

    for (;;) {
        const bool more = drain(child.out_fd, result.output, truncated);
        if (!reaped) {
            if (waitpid(child.pid, &status, WNOHANG) == child.pid) {
                reaped = true;
            } else if (std::chrono::steady_clock::now() >= deadline) {
                kill(child.pid, SIGKILL);
                result.timed_out = true;
                waitpid(child.pid, &status, 0);
                reaped = true;
            }
        }
        if (reaped) {
            // Take what is left in the pipe. A grandchild that inherited the pipe must
            // not keep us waiting, so this never blocks.
            drain(child.out_fd, result.output, truncated);
            break;
        }
        if (more) {
            pollfd p{child.out_fd, POLLIN, 0};
            poll(&p, 1, 20);
        } else {
            usleep(10000);  // end of output but the child still runs: wait for exit
        }
    }
    ::close(child.out_fd);

    if (!result.timed_out && WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
    if (truncated) result.output += "\n... output truncated\n";
    return result;
}

// Writes the absolute paths of every .java file under `source_dir` to out/<list_name>.
// Quoted for javac's @argfile syntax. Returns the file count, or -1 on error.
long write_source_list(const fs::path& source_dir, const fs::path& list_path, std::string& error) {
    std::error_code ec;
    std::vector<std::string> files;
    for (fs::recursive_directory_iterator it(source_dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code fec;
        if (it->is_regular_file(fec) && it->path().extension() == ".java") {
            files.push_back(fs::absolute(it->path()).string());
        }
    }
    if (files.empty()) {
        error = "No .java files under " + source_dir.string();
        return -1;
    }
    std::sort(files.begin(), files.end());

    std::ofstream out(list_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Cannot write " + list_path.string();
        return -1;
    }
    for (const std::string& f : files) {
        out << '"';
        for (char c : f) {
            if (c == '"' || c == '\\') out << '\\';
            out << c;
        }
        out << "\"\n";
    }
    out.flush();
    if (!out) {
        error = "Write failed for " + list_path.string();
        return -1;
    }
    return static_cast<long>(files.size());
}

// javac over source_dir into out_dir. `classpath` may be empty.
RunResult javac_dir(const fs::path& root, const fs::path& source_dir, const fs::path& out_dir,
                    const std::string& classpath, const char* list_name) {
    RunResult result;
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        result.output = "Cannot create " + out_dir.string() + ": " + ec.message() + "\n";
        return result;
    }
    const fs::path list_path = root / "out" / list_name;
    std::string error;
    if (write_source_list(source_dir, list_path, error) < 0) {
        result.output = error + "\n";
        return result;
    }

    std::vector<std::string> argv = {"javac", "-encoding", "UTF-8", "--release", "17", "-d",
                                     out_dir.string()};
    if (!classpath.empty()) {
        argv.push_back("-cp");
        argv.push_back(classpath);
    }
    argv.push_back("@" + list_path.string());

    result = run_in(argv, "", kCompileTimeout);
    fs::remove(list_path, ec);
    return result;
}

}  // namespace

RunResult run_process(const std::vector<std::string>& argv, double timeout_seconds) {
    return run_in(argv, "", timeout_seconds);
}

RunResult compile_project(const std::string& project_dir) {
    const fs::path root = fs::absolute(project_dir);
    return javac_dir(root, root / "src", root / "out" / "classes", "", "javac-sources.txt");
}

RunResult run_tests(const std::string& project_dir, const std::string& main_class) {
    const fs::path root = fs::absolute(project_dir);
    RunResult compiled = javac_dir(root, root / "test", root / "out" / "test-classes",
                                   (root / "out" / "classes").string(), "javac-test-sources.txt");
    if (compiled.exit_code != 0) return compiled;

    return run_in({"java", "-cp", "out/classes:out/test-classes", main_class}, root.string(),
                  kTestTimeout);
}

std::vector<Diagnostic> parse_javac_output(const std::string& output) {
    static const std::string kMarker = ": error: ";
    std::vector<Diagnostic> found;
    std::size_t start = 0;
    while (start < output.size()) {
        std::size_t end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();
        const std::string line = output.substr(start, end - start);
        start = end + 1;

        const std::size_t at = line.find(kMarker);
        if (at == std::string::npos) continue;
        const std::string head = line.substr(0, at);  // "<path>:<line>"
        const std::size_t colon = head.rfind(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= head.size()) continue;

        bool digits = true;
        for (std::size_t i = colon + 1; i < head.size(); ++i) {
            if (head[i] < '0' || head[i] > '9') digits = false;
        }
        if (!digits) continue;

        Diagnostic d;
        d.file = head.substr(0, colon);
        d.line = std::atoi(head.c_str() + colon + 1);
        d.message = line.substr(at + kMarker.size());
        found.push_back(std::move(d));
    }
    return found;
}

}  // namespace wbx
