#include "apply_fix.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <vector>

#include "java_tools.hpp"

namespace fs = std::filesystem;

namespace app::apply_fix {
namespace {

constexpr std::size_t kContext = 3;

// Splits text into lines. Each line keeps its '\n', so a missing final newline is part of
// the line and shows up in the diff.
std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t nl = text.find('\n', start);
        const std::size_t end = nl == std::string::npos ? text.size() : nl + 1;
        lines.push_back(text.substr(start, end - start));
        start = end;
    }
    return lines;
}

enum class Op : unsigned char { equal, del, ins };

struct Edit {
    Op op;
    std::size_t a;  // index into old lines (equal, del)
    std::size_t b;  // index into new lines (equal, ins)
};

// Myers' O(ND) greedy search in linear space (Myers 1986, section 4b). Finds the middle snake
// of the region, then recurses on the two halves. Time O((N+M)·D), space O(N+M).
// Common prefixes and suffixes are emitted without search. A region with an empty side is
// all inserts or all deletes.
struct Snake {
    long x, y;  // start, region-relative
    long u, v;  // end, region-relative
    long d;     // edit count of the whole region when the snake was found
};

struct Differ {
    const std::vector<std::string>& a;
    const std::vector<std::string>& b;
    std::vector<long> vf, vb;  // furthest x per diagonal, forward and reverse; index = k + off
    long off;
    std::vector<Edit>& out;

    Differ(const std::vector<std::string>& a_, const std::vector<std::string>& b_, std::vector<Edit>& out_)
        : a(a_), b(b_), off(static_cast<long>(a_.size() + b_.size()) + 1), out(out_) {
        const std::size_t width = static_cast<std::size_t>(2 * (a_.size() + b_.size()) + 3);
        vf.assign(width, 0);
        vb.assign(width, 0);
    }

    // Middle snake of a[al, al+N) against b[bl, bl+M), with N and M both > 0 after trimming.
    Snake middle_snake(std::size_t al, std::size_t bl, long N, long M) {
        const long delta = N - M;
        const bool odd = (delta & 1) != 0;
        vf[static_cast<std::size_t>(off + 1)] = 0;
        vb[static_cast<std::size_t>(off + 1)] = 0;
        const long max_d = (N + M + 1) / 2;
        for (long d = 0; d <= max_d; ++d) {
            for (long k = -d; k <= d; k += 2) {
                long x = (k == -d || (k != d && vf[idx(k - 1)] < vf[idx(k + 1)])) ? vf[idx(k + 1)]
                                                                                 : vf[idx(k - 1)] + 1;
                long y = x - k;
                const long x0 = x, y0 = y;
                while (x < N && y < M && a[al + static_cast<std::size_t>(x)] == b[bl + static_cast<std::size_t>(y)]) {
                    ++x;
                    ++y;
                }
                vf[idx(k)] = x;
                if (odd && k >= delta - (d - 1) && k <= delta + (d - 1) && vf[idx(k)] + vb[idx(delta - k)] >= N) {
                    return {x0, y0, x, y, 2 * d - 1};
                }
            }
            for (long k = -d; k <= d; k += 2) {
                long x = (k == -d || (k != d && vb[idx(k - 1)] < vb[idx(k + 1)])) ? vb[idx(k + 1)]
                                                                                 : vb[idx(k - 1)] + 1;
                long y = x - k;
                const long x0 = x, y0 = y;
                while (x < N && y < M &&
                       a[al + static_cast<std::size_t>(N - 1 - x)] == b[bl + static_cast<std::size_t>(M - 1 - y)]) {
                    ++x;
                    ++y;
                }
                vb[idx(k)] = x;
                if (!odd && k >= delta - d && k <= delta + d && vb[idx(k)] + vf[idx(delta - k)] >= N) {
                    return {N - x, M - y, N - x0, M - y0, 2 * d};
                }
            }
        }
        return {0, 0, 0, 0, 0};  // not reached: the search always meets in the middle
    }

    std::size_t idx(long k) const { return static_cast<std::size_t>(off + k); }

    // Appends the edit script for a[al, ar) against b[bl, br).
    void solve(std::size_t al, std::size_t ar, std::size_t bl, std::size_t br) {
        while (al < ar && bl < br && a[al] == b[bl]) {
            out.push_back({Op::equal, al, bl});
            ++al;
            ++bl;
        }
        std::size_t tail = 0;
        while (al < ar - tail && bl < br - tail && a[ar - 1 - tail] == b[br - 1 - tail]) ++tail;
        ar -= tail;
        br -= tail;
        const long N = static_cast<long>(ar - al);
        const long M = static_cast<long>(br - bl);
        if (N == 0) {
            for (std::size_t j = bl; j < br; ++j) out.push_back({Op::ins, 0, j});
        } else if (M == 0) {
            for (std::size_t i = al; i < ar; ++i) out.push_back({Op::del, i, 0});
        } else {
            const Snake s = middle_snake(al, bl, N, M);
            const std::size_t x = al + static_cast<std::size_t>(s.x), y = bl + static_cast<std::size_t>(s.y);
            const std::size_t u = al + static_cast<std::size_t>(s.u), v = bl + static_cast<std::size_t>(s.v);
            solve(al, x, bl, y);
            for (std::size_t i = x; i < u; ++i) out.push_back({Op::equal, i, y + (i - x)});
            solve(u, ar, v, br);
        }
        for (std::size_t t = tail; t > 0; --t) out.push_back({Op::equal, ar + (tail - t), br + (tail - t)});
    }
};

std::vector<Edit> myers(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::vector<Edit> script;
    Differ(a, b, script).solve(0, a.size(), 0, b.size());
    return script;
}

std::string range(std::size_t start0, std::size_t count) {
    // Unified diff: an empty range names the line before it, and a count of 1 is omitted.
    const std::size_t shown = count == 0 ? start0 : start0 + 1;
    std::string s = std::to_string(shown);
    if (count != 1) s += "," + std::to_string(count);
    return s;
}

std::string with_newline_marker(const std::string& line) {
    if (!line.empty() && line.back() == '\n') return line;
    return line + "\n\\ No newline at end of file\n";
}

// ---- staging and apply helpers ----------------------------------------------

bool valid_class_path(const std::string& class_path) {
    if (class_path.compare(0, 4, "src/") != 0) return false;
    const fs::path p(class_path);
    if (p.is_absolute()) return false;
    for (const auto& part : p) {
        if (part == "..") return false;
    }
    return p.extension() == ".java";
}

bool read_text(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

bool write_text(const fs::path& path, const std::string& text) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    if (ec) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    out.flush();
    return static_cast<bool>(out);
}

// Removes the staging directory when stage() returns, however it returns.
struct TempDir {
    fs::path path;
    ~TempDir() {
        std::error_code ec;
        if (!path.empty()) fs::remove_all(path, ec);
    }
};

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t secs = std::chrono::system_clock::to_time_t(now);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    localtime_r(&secs, &tm);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d%02d%02d-%02d%02d%02d-%03d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}

}  // namespace

std::string unified_diff(const std::string& old_text, const std::string& new_text,
                         const std::string& path) {
    const std::vector<std::string> a = split_lines(old_text);
    const std::vector<std::string> b = split_lines(new_text);
    const std::vector<Edit> script = myers(a, b);

    // Hunks: changes separated by at most 2 * kContext equal lines share one hunk.
    std::vector<std::size_t> changes;
    for (std::size_t i = 0; i < script.size(); ++i) {
        if (script[i].op != Op::equal) changes.push_back(i);
    }
    if (changes.empty()) return {};

    std::string out = "--- a/" + path + "\n+++ b/" + path + "\n";
    std::size_t i = 0;
    while (i < changes.size()) {
        std::size_t last = changes[i];
        std::size_t j = i + 1;
        while (j < changes.size() && changes[j] - last - 1 <= 2 * kContext) {
            last = changes[j];
            ++j;
        }
        const std::size_t first = changes[i];
        const std::size_t lo = first > kContext ? first - kContext : 0;
        const std::size_t hi = std::min(script.size() - 1, last + kContext);

        // Line numbers before the hunk, from the edits that come before it.
        std::size_t old_before = 0;
        std::size_t new_before = 0;
        for (std::size_t k = 0; k < lo; ++k) {
            if (script[k].op != Op::ins) ++old_before;
            if (script[k].op != Op::del) ++new_before;
        }
        std::size_t old_count = 0;
        std::size_t new_count = 0;
        std::string body;
        for (std::size_t k = lo; k <= hi; ++k) {
            const Edit& e = script[k];
            switch (e.op) {
                case Op::equal:
                    ++old_count;
                    ++new_count;
                    body += " " + with_newline_marker(a[e.a]);
                    break;
                case Op::del:
                    ++old_count;
                    body += "-" + with_newline_marker(a[e.a]);
                    break;
                case Op::ins:
                    ++new_count;
                    body += "+" + with_newline_marker(b[e.b]);
                    break;
            }
        }
        out += "@@ -" + range(old_before, old_count) + " +" + range(new_before, new_count) + " @@\n";
        out += body;
        i = j;
    }
    return out;
}

StageResult stage(const std::string& project_dir, const fixer::Proposal& proposal,
                  const std::string& main_class) {
    StageResult result;
    if (proposal.edits.empty()) {
        result.error = "proposal has no edits";
        return result;
    }
    for (const fixer::FileEdit& e : proposal.edits) {
        if (!valid_class_path(e.class_path)) {
            result.error = "proposal path must be a src/ .java path: '" + e.class_path + "'";
            return result;
        }
    }
    const fs::path root = fs::absolute(project_dir);

    // A random suffix keeps concurrent stagings apart.
    std::random_device rd;
    TempDir tmp{fs::temp_directory_path() / ("wb-stage-" + std::to_string(rd()) + std::to_string(rd()))};
    std::error_code ec;
    fs::create_directories(tmp.path, ec);
    if (ec) {
        result.error = "cannot create staging directory: " + ec.message();
        return result;
    }
    for (const char* dir : {"src", "test"}) {
        if (!fs::is_directory(root / dir, ec)) continue;
        fs::copy(root / dir, tmp.path / dir, fs::copy_options::recursive, ec);
        if (ec) {
            result.error = std::string("cannot copy ") + dir + ": " + ec.message();
            return result;
        }
    }
    for (const fixer::FileEdit& e : proposal.edits) {
        if (!write_text(tmp.path / e.class_path, e.new_source)) {
            result.error = "cannot write " + e.class_path + " into the staging copy";
            return result;
        }
    }

    const wbx::RunResult compiled = wbx::compile_project(tmp.path.string());
    result.compile_output = compiled.output;
    result.compiled = compiled.exit_code == 0;
    if (!result.compiled) return result;

    const wbx::RunResult tests = wbx::run_tests(tmp.path.string(), main_class);
    result.test_output = tests.output;
    result.tested = tests.exit_code == 0 && !tests.timed_out;
    return result;
}

ApplyResult apply(const std::string& project_dir, const fixer::Proposal& proposal,
                  const StageResult& staged, bool confirmed) {
    ApplyResult result;
    if (!confirmed) {
        result.error = "not confirmed; nothing written";
        return result;
    }
    if (!staged.green()) {
        result.error = staged.error.empty() ? "staged build or tests did not pass; nothing written"
                                            : "staging failed: " + staged.error;
        return result;
    }
    if (proposal.edits.empty()) {
        result.error = "proposal has no edits; nothing written";
        return result;
    }

    // Check every file before writing any, so a conflict never leaves a half-applied proposal.
    const fs::path root = fs::absolute(project_dir);
    std::vector<std::string> currents;
    for (const fixer::FileEdit& e : proposal.edits) {
        if (!valid_class_path(e.class_path)) {
            result.error = "proposal path must be a src/ .java path; nothing written";
            return result;
        }
        std::string current;
        if (!read_text(root / e.class_path, current)) {
            result.error = "cannot read " + e.class_path + "; nothing written";
            return result;
        }
        if (current != e.base_source) {
            result.conflict = true;
            result.error = "file changed since the proposal was made: " + e.class_path +
                           "; nothing written";
            return result;
        }
        currents.push_back(std::move(current));
    }

    const fs::path backup_dir = root / "out" / "backups";
    const std::string stamp = timestamp();
    for (std::size_t i = 0; i < proposal.edits.size(); ++i) {
        const fs::path target = root / proposal.edits[i].class_path;
        const fs::path backup = backup_dir / (stamp + "_" + target.stem().string() + ".java");
        if (!write_text(backup, currents[i])) {
            result.error = "cannot write backup " + backup.string() + "; nothing written";
            return result;
        }
        result.backups.push_back(backup.string());
    }
    result.backup_path = result.backups.empty() ? std::string() : result.backups.back();

    for (std::size_t i = 0; i < proposal.edits.size(); ++i) {
        const fs::path target = root / proposal.edits[i].class_path;
        if (!write_text(target, proposal.edits[i].new_source)) {
            result.error = "cannot write " + target.string() + "; backups are in " + backup_dir.string();
            return result;
        }
        result.written_paths.push_back(proposal.edits[i].class_path);
    }
    result.written = true;
    return result;
}

}  // namespace app::apply_fix
