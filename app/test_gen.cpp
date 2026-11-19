#include "test_gen.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

#include "architect.hpp"
#include "scaffold.hpp"
#include "session.hpp"

namespace fs = std::filesystem;

namespace app::test_gen {
namespace {

bool is_type_kind(wb::SymbolKind kind) {
    return kind == wb::SymbolKind::Class || kind == wb::SymbolKind::Interface ||
           kind == wb::SymbolKind::Enum || kind == wb::SymbolKind::Record;
}

std::string rel_string(const fs::path& path, const fs::path& base) {
    std::error_code ec;
    const fs::path rel = fs::relative(path, base, ec);
    return (ec ? path : rel).generic_string();
}

// Every .java file under `dir`, sorted. Empty when `dir` is not a directory.
std::vector<fs::path> java_files(const fs::path& dir) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return files;
    for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code fec;
        if (it->is_regular_file(fec) && it->path().extension() == ".java") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool read_file(const fs::path& path, std::string& out) {
    std::error_code ec;
    if (fs::file_size(path, ec) > app::architect::kMaxFileBytes) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

// Writes `text` only if `path` does not exist yet ("x" mode: the check and the create are one
// step). Creates parent folders as needed.
bool create_new_file(const fs::path& path, const std::string& text, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "cannot create " + path.parent_path().string() + ": " + ec.message();
        return false;
    }
    std::FILE* f = std::fopen(path.string().c_str(), "wbx");
    if (f == nullptr) {
        error = errno == EEXIST ? "already exists" : std::strerror(errno);
        return false;
    }
    const bool wrote = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool closed = std::fclose(f) == 0;
    if (!wrote || !closed) {
        error = "write failed";
        return false;
    }
    return true;
}

}  // namespace

ParsedSource parse_source(const std::string& rel_path, const std::string& text, wb::Session& session) {
    session.set_text(text);
    ParsedSource out;
    out.rel_path = rel_path;
    out.package = app::architect::package_name(text);
    out.symbols = session.analyze().symbols;
    return out;
}

std::vector<TopLevelType> top_level_types(const std::vector<ParsedSource>& sources) {
    std::vector<TopLevelType> out;
    for (std::size_t s = 0; s < sources.size(); ++s) {
        const std::vector<wb::Symbol>& symbols = sources[s].symbols;
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i].parent != -1 || !is_type_kind(symbols[i].kind)) continue;
            out.push_back(TopLevelType{symbols[i].name, s, i});
        }
    }
    return out;
}

std::vector<std::string> test_class_names(const std::vector<std::string>& test_file_names) {
    static const std::string kSuffix = "Test";
    std::vector<std::string> out;
    for (const std::string& name : test_file_names) {
        // Keep the last path component, then drop the ".java" extension if present.
        std::string stem = fs::path(name).filename().string();
        if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, ".java") == 0) stem.resize(stem.size() - 5);
        if (stem.size() > kSuffix.size() &&
            stem.compare(stem.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
            out.push_back(stem);
        }
    }
    return out;
}

fs::path test_file_path(const fs::path& test_root, const std::string& package, const std::string& type_name) {
    fs::path dir = test_root;
    std::size_t begin = 0;
    while (begin < package.size()) {
        std::size_t dot = package.find('.', begin);
        if (dot == std::string::npos) dot = package.size();
        if (dot > begin) dir /= package.substr(begin, dot - begin);
        begin = dot + 1;
    }
    return dir / (type_name + "Test.java");
}

std::vector<NewTest> plan_missing(const std::vector<ParsedSource>& sources,
                                  const std::vector<std::string>& test_names,
                                  const fs::path& test_root,
                                  const std::function<bool(const fs::path&)>& exists) {
    const std::vector<TopLevelType> types = top_level_types(sources);

    // The core decides which names are missing; the flat list holds only top-level symbols.
    std::vector<wb::Symbol> top;
    top.reserve(types.size());
    for (const TopLevelType& t : types) top.push_back(sources[t.source].symbols[t.symbol]);
    const std::vector<std::string> missing_names = wb::missing_test_classes(top, test_names);
    const std::set<std::string> missing(missing_names.begin(), missing_names.end());

    std::vector<NewTest> plan;
    std::set<fs::path> planned;
    for (const TopLevelType& t : types) {
        if (missing.count(t.name) == 0) continue;
        const ParsedSource& src = sources[t.source];
        const fs::path path = test_file_path(test_root, src.package, t.name);
        if (planned.count(path) != 0) continue;  // same type declared twice in one package
        if (exists && exists(path)) continue;
        planned.insert(path);
        plan.push_back(NewTest{path, t.name,
                               wb::scaffold_test_class(src.package, src.symbols, t.symbol)});
    }
    return plan;
}

Report generate_missing_tests(const fs::path& project_dir) {
    const auto started = std::chrono::steady_clock::now();
    Report report;
    const fs::path root = fs::absolute(project_dir);
    const fs::path src_dir = root / "src";
    const fs::path test_dir = root / "test";

    std::error_code ec;
    if (!fs::is_directory(src_dir, ec)) {
        report.failed = true;
        report.errors.push_back("no src folder under " + root.string());
    } else {
        // Parse every source. Files over the size cap are reported and left out.
        wb::Session session;
        std::vector<ParsedSource> sources;
        for (const fs::path& file : java_files(src_dir)) {
            const std::string rel = rel_string(file, src_dir);
            std::string text;
            if (!read_file(file, text)) {
                report.errors.push_back("skipped " + rel + ": unreadable or over 4 MiB");
                continue;
            }
            sources.push_back(parse_source(rel, text, session));
        }
        report.source_files = sources.size();
        report.top_level_types = top_level_types(sources).size();

        if (sources.empty()) {
            report.failed = true;
            report.errors.push_back("no .java files under " + src_dir.string());
        } else {
            std::vector<std::string> test_files;
            for (const fs::path& file : java_files(test_dir)) test_files.push_back(file.filename().string());
            const std::vector<std::string> tests = test_class_names(test_files);
            report.test_classes = tests.size();

            const std::vector<NewTest> plan = plan_missing(
                sources, tests, test_dir, [](const fs::path& p) {
                    std::error_code exists_ec;
                    return fs::exists(p, exists_ec);
                });
            for (const NewTest& t : plan) {
                std::string error;
                if (create_new_file(t.path, t.contents, error)) {
                    report.created.push_back(t.path);
                } else {
                    report.errors.push_back("cannot write " + rel_string(t.path, root) + ": " + error);
                }
            }
        }
    }

    report.elapsed_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return report;
}

std::string format_report(const Report& report, const fs::path& project_dir) {
    std::ostringstream out;
    const fs::path root = fs::absolute(project_dir);
    char ms[32];
    std::snprintf(ms, sizeof ms, "%.1f ms", report.elapsed_ms);

    if (report.failed) {
        for (const std::string& e : report.errors) out << e << "\n";
        return out.str();
    }
    out << "Tests for missing classes: " << report.source_files << " source files, "
        << report.top_level_types << " top-level types, " << report.test_classes
        << " test classes (" << ms << ")\n";
    if (report.created.empty()) {
        out << "Nothing missing.\n";
    } else {
        out << "Created " << report.created.size() << " file(s):\n";
        for (const fs::path& p : report.created) out << "  " << rel_string(p, root) << "\n";
    }
    for (const std::string& e : report.errors) out << e << "\n";
    return out.str();
}

}  // namespace app::test_gen
