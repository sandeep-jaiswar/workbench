#include "architect.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

#include "session.hpp"

namespace fs = std::filesystem;

namespace app::architect {

// The first "package x.y;" line, or empty. Good enough for a summary: comments are not parsed.
std::string package_name(const std::string& text) {
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::size_t i = start;
        while (i < end && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r')) ++i;
        if (text.compare(i, 8, "package ") == 0) {
            std::size_t semi = text.find(';', i);
            if (semi == std::string::npos || semi > end) return {};
            std::string name = text.substr(i + 8, semi - i - 8);
            name.erase(std::remove_if(name.begin(), name.end(),
                                      [](char c) { return c == ' ' || c == '\t' || c == '\r'; }),
                       name.end());
            return name;
        }
        start = end + 1;
    }
    return {};
}

namespace {

std::string type_kind(wb::SymbolKind kind) {
    switch (kind) {
        case wb::SymbolKind::Interface: return "interface";
        case wb::SymbolKind::Enum: return "enum";
        case wb::SymbolKind::Record: return "record";
        default: return "class";
    }
}

bool is_type(wb::SymbolKind kind) {
    return kind == wb::SymbolKind::Class || kind == wb::SymbolKind::Interface ||
           kind == wb::SymbolKind::Enum || kind == wb::SymbolKind::Record;
}

FileEntry outline_one(const std::string& path, const std::string& text, wb::Session& session,
                      Outline& out, std::vector<architect_rules::FileReport>& reports) {
    session.set_text(text);
    wb::Report report = session.analyze();
    FileEntry entry;
    entry.path = path;
    entry.package = package_name(text);

    // Map each type symbol to its index in entry.types; members point back via `parent`.
    std::vector<std::int32_t> type_slot(report.symbols.size(), -1);
    for (std::size_t i = 0; i < report.symbols.size(); ++i) {
        const wb::Symbol& sym = report.symbols[i];
        if (!is_type(sym.kind)) continue;
        TypeEntry t;
        t.name = sym.name;
        t.kind = type_kind(sym.kind);
        t.nested = sym.parent >= 0;
        type_slot[i] = static_cast<std::int32_t>(entry.types.size());
        entry.types.push_back(std::move(t));
    }
    for (const wb::Symbol& sym : report.symbols) {
        if (sym.parent < 0 || static_cast<std::size_t>(sym.parent) >= type_slot.size()) continue;
        const std::int32_t slot = type_slot[static_cast<std::size_t>(sym.parent)];
        if (slot < 0) continue;
        if (sym.kind == wb::SymbolKind::Method) {
            ++entry.types[static_cast<std::size_t>(slot)].methods;
        } else if (sym.kind == wb::SymbolKind::Field) {
            ++entry.types[static_cast<std::size_t>(slot)].fields;
        }
    }
    for (const TypeEntry& t : entry.types) {
        ++out.types;
        out.methods += t.methods;
        out.fields += t.fields;
    }
    ++out.files;
    reports.push_back(architect_rules::FileReport{path, std::move(report)});
    return entry;
}

}  // namespace

Outline outline_sources(const std::vector<Source>& sources, const std::vector<std::string>& test_names) {
    Outline out;
    wb::Session session;
    std::vector<architect_rules::FileReport> reports;
    for (const Source& src : sources) {
        if (out.files >= kMaxFiles) {
            out.truncated = true;
            break;
        }
        if (src.text.size() > kMaxFileBytes) {
            out.skipped.push_back(Skipped{src.path, "over 4 MiB"});
            continue;
        }
        out.entries.push_back(outline_one(src.path, src.text, session, out, reports));
    }
    out.rules = architect_rules::analyze(reports, test_names);
    return out;
}

Outline outline_project(const std::string& project_dir) {
    const fs::path src_dir = fs::absolute(project_dir) / "src";
    std::error_code ec;
    if (!fs::is_directory(src_dir, ec)) {
        throw std::runtime_error("no src directory under " + project_dir);
    }

    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(src_dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code fec;
        if (it->is_regular_file(fec) && it->path().extension() == ".java") {
            files.push_back(it->path());
        }
    }
    if (files.empty()) throw std::runtime_error("no .java files under " + src_dir.string());
    std::sort(files.begin(), files.end());

    // Read at most kMaxFiles + the skipped ones; the cap counts parsed files in outline_sources.
    std::vector<Source> sources;
    std::vector<Skipped> skipped;
    for (const fs::path& file : files) {
        if (sources.size() >= kMaxFiles) break;
        std::error_code rel_ec;
        const std::string rel = fs::relative(file, src_dir, rel_ec).generic_string();
        std::error_code size_ec;
        const std::uintmax_t size = fs::file_size(file, size_ec);
        if (size_ec) {
            skipped.push_back(Skipped{rel, "cannot read"});
            continue;
        }
        if (size > kMaxFileBytes) {
            skipped.push_back(Skipped{rel, "over 4 MiB"});
            continue;
        }
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            skipped.push_back(Skipped{rel, "cannot open"});
            continue;
        }
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        sources.push_back(Source{rel, std::move(text)});
    }

    // Test class names: stems of the .java files under <project>/test, if the folder exists.
    std::vector<std::string> test_names;
    const fs::path test_dir = fs::absolute(project_dir) / "test";
    if (fs::is_directory(test_dir, ec)) {
        for (fs::recursive_directory_iterator it(test_dir, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            std::error_code fec;
            if (it->is_regular_file(fec) && it->path().extension() == ".java") {
                test_names.push_back(it->path().stem().string());
            }
        }
        std::sort(test_names.begin(), test_names.end());
    }

    Outline out = outline_sources(sources, test_names);
    out.truncated = files.size() > kMaxFiles;
    out.skipped.insert(out.skipped.end(), skipped.begin(), skipped.end());
    return out;
}

std::string summarize(const Outline& outline) {
    std::string s = std::to_string(outline.files) + " files, " + std::to_string(outline.types) +
                    " types, " + std::to_string(outline.methods) + " methods";
    if (!outline.skipped.empty()) s += ", " + std::to_string(outline.skipped.size()) + " skipped";
    if (outline.truncated) s += ", capped at " + std::to_string(kMaxFiles) + " files";
    const auto& findings = outline.rules.findings;
    s += ", " + std::to_string(findings.size()) + " findings";
    constexpr std::size_t kShown = 5;
    for (std::size_t i = 0; i < findings.size() && i < kShown; ++i) {
        s += i == 0 ? ": " : "; ";
        s += findings[i].rule + " " + findings[i].file + ":" + std::to_string(findings[i].line);
    }
    if (findings.size() > kShown) s += "; +" + std::to_string(findings.size() - kShown) + " more";
    return s;
}

}  // namespace app::architect
