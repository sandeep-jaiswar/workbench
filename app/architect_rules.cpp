#include "architect_rules.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "architect.hpp"
#include "scaffold.hpp"
#include "session.hpp"

namespace app::architect_rules {
namespace {

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

bool is_type_kind(wb::SymbolKind k) {
    return k == wb::SymbolKind::Class || k == wb::SymbolKind::Interface ||
           k == wb::SymbolKind::Enum || k == wb::SymbolKind::Record;
}

bool has_modifier(const wb::Symbol& s, const char* word) {
    return std::find(s.modifiers.begin(), s.modifiers.end(), word) != s.modifiers.end();
}

std::uint64_t pair_key(std::size_t a, std::size_t b) {
    return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);
}

// Identifiers in `text`, in order. "Map<String,Integer>" yields Map, String, Integer.
void identifiers(std::string_view text, std::vector<std::string>& out) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (!(std::isalpha(c) || c == '_' || c == '$')) {
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j < text.size()) {
            const auto d = static_cast<unsigned char>(text[j]);
            if (!(std::isalnum(d) || d == '_' || d == '$')) break;
            ++j;
        }
        out.emplace_back(text.substr(i, j - i));
        i = j;
    }
}

// Simple names an import can refer to. "java.util.List" gives List. "static a.B.max" gives max
// and B. Wildcard imports name no type.
void import_names(const std::string& text, std::vector<std::string>& out) {
    std::string body = text;
    bool is_static = false;
    if (body.rfind("static ", 0) == 0) {
        is_static = true;
        body = body.substr(7);
    }
    if (body.empty() || body.back() == '*') return;
    const std::size_t dot = body.rfind('.');
    if (dot == std::string::npos) {
        out.push_back(body);
        return;
    }
    out.push_back(body.substr(dot + 1));
    if (is_static) {
        const std::string head = body.substr(0, dot);
        const std::size_t dot2 = head.rfind('.');
        out.push_back(head.substr(dot2 == std::string::npos ? 0 : dot2 + 1));
    }
}

std::string display_package(const std::string& pkg) {
    return pkg.empty() ? std::string("(default)") : pkg;
}

// Tarjan's strongly connected components, iterative so depth is not bounded by the stack.
// Returns components as lists of vertex ids.
std::vector<std::vector<std::size_t>> strongly_connected(const std::vector<std::vector<std::size_t>>& adj) {
    const std::size_t n = adj.size();
    std::vector<std::size_t> index(n, kNone);
    std::vector<std::size_t> low(n, 0);
    std::vector<bool> on_stack(n, false);
    std::vector<std::size_t> stack;
    std::vector<std::vector<std::size_t>> out;
    struct Frame {
        std::size_t v;
        std::size_t next;
    };
    std::vector<Frame> call;
    std::size_t counter = 0;

    for (std::size_t s = 0; s < n; ++s) {
        if (index[s] != kNone) continue;
        index[s] = low[s] = counter++;
        stack.push_back(s);
        on_stack[s] = true;
        call.push_back({s, 0});
        while (!call.empty()) {
            const std::size_t v = call.back().v;
            if (call.back().next < adj[v].size()) {
                const std::size_t w = adj[v][call.back().next++];
                if (index[w] == kNone) {
                    index[w] = low[w] = counter++;
                    stack.push_back(w);
                    on_stack[w] = true;
                    call.push_back({w, 0});  // invalidates the reference above; not used after
                } else if (on_stack[w]) {
                    low[v] = std::min(low[v], index[w]);
                }
                continue;
            }
            if (low[v] == index[v]) {
                std::vector<std::size_t> comp;
                std::size_t w;
                do {
                    w = stack.back();
                    stack.pop_back();
                    on_stack[w] = false;
                    comp.push_back(w);
                } while (w != v);
                out.push_back(std::move(comp));
            }
            call.pop_back();
            if (!call.empty()) {
                const std::size_t parent = call.back().v;
                low[parent] = std::min(low[parent], low[v]);
            }
        }
    }
    return out;
}

}  // namespace

bool Finding::operator==(const Finding& o) const {
    return rule == o.rule && severity == o.severity && file == o.file && line == o.line &&
           message == o.message;
}

bool Edge::operator==(const Edge& o) const {
    return from == o.from && to == o.to;
}

Architecture analyze(const std::vector<FileReport>& files, const std::vector<std::string>& test_names) {
    Architecture arch;
    arch.files = files.size();

    // Top-level types of all files, in file order. Each file's types are a contiguous range.
    struct TopType {
        std::size_t file;
        std::size_t sym;
        std::size_t pkg;
        std::string name;
        std::string qname;
    };
    std::vector<TopType> tops;
    std::vector<std::string> pkg_names;
    std::unordered_map<std::string, std::size_t> pkg_ids;
    auto package_id = [&](const std::string& pkg) {
        const auto it = pkg_ids.find(pkg);
        if (it != pkg_ids.end()) return it->second;
        pkg_names.push_back(pkg);
        pkg_ids.emplace(pkg, pkg_names.size() - 1);
        return pkg_names.size() - 1;
    };
    std::vector<std::pair<std::size_t, std::size_t>> file_range(files.size());
    // For each symbol, the global index of the top-level type that encloses it (kNone if none).
    std::vector<std::vector<std::size_t>> top_of(files.size());

    for (std::size_t fi = 0; fi < files.size(); ++fi) {
        const wb::Report& rep = files[fi].report;
        const std::size_t begin = tops.size();
        const std::size_t pkg = package_id(rep.package);
        std::vector<std::size_t>& owner = top_of[fi];
        owner.assign(rep.symbols.size(), kNone);
        for (std::size_t j = 0; j < rep.symbols.size(); ++j) {
            const wb::Symbol& s = rep.symbols[j];
            if (s.kind == wb::SymbolKind::Method) ++arch.methods;
            if (s.parent >= 0 && static_cast<std::size_t>(s.parent) < j) {
                owner[j] = owner[static_cast<std::size_t>(s.parent)];  // parents precede children
            }
            if (!is_type_kind(s.kind)) continue;
            ++arch.types;
            if (s.parent != -1) continue;
            owner[j] = tops.size();
            TopType t;
            t.file = fi;
            t.sym = j;
            t.pkg = pkg;
            t.name = s.name;
            t.qname = rep.package.empty() ? s.name : rep.package + "." + s.name;
            tops.push_back(std::move(t));
        }
        file_range[fi] = {begin, tops.size()};
    }

    // Simple name -> top-level type index. A name declared twice is ambiguous: no edges to it.
    std::unordered_map<std::string, std::size_t> by_name;
    by_name.reserve(tops.size());
    for (std::size_t i = 0; i < tops.size(); ++i) {
        const auto [it, inserted] = by_name.emplace(tops[i].name, i);
        if (!inserted && it->second != i) it->second = kNone;
    }

    // Type edges. A top-level type A references the project types named by the type texts,
    // return types and parameters of A's own members (nested types included), plus the types
    // named in its file's imports (which apply to every top-level type of the file).
    std::vector<std::pair<std::size_t, std::size_t>> type_edges;
    std::unordered_set<std::uint64_t> seen;
    std::vector<std::string> names;
    for (std::size_t fi = 0; fi < files.size(); ++fi) {
        const auto [begin, end] = file_range[fi];
        if (begin == end) continue;
        const wb::Report& rep = files[fi].report;
        auto lookup = [&](const std::string& name) {
            const auto it = by_name.find(name);
            return it != by_name.end() ? it->second : kNone;  // kNone also marks ambiguous names
        };
        std::vector<std::vector<std::size_t>> own(end - begin);  // targets per top-level type
        for (std::size_t j = 0; j < rep.symbols.size(); ++j) {
            const std::size_t owner = top_of[fi][j];
            if (owner == kNone) continue;
            const wb::Symbol& s = rep.symbols[j];
            names.clear();
            identifiers(s.type_text, names);
            identifiers(s.return_type, names);
            for (const std::string& p : s.params) identifiers(p, names);
            for (const std::string& n : names) {
                const std::size_t t = lookup(n);
                if (t != kNone) own[owner - begin].push_back(t);
            }
        }
        std::vector<std::size_t> from_imports;
        for (const wb::Import& imp : rep.imports) {
            names.clear();
            import_names(imp.text, names);
            for (const std::string& n : names) {
                const std::size_t t = lookup(n);
                if (t != kNone) from_imports.push_back(t);
            }
        }
        for (std::size_t a = begin; a < end; ++a) {
            std::vector<std::size_t>& targets = own[a - begin];
            targets.insert(targets.end(), from_imports.begin(), from_imports.end());
            std::sort(targets.begin(), targets.end());
            targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
            for (std::size_t b : targets) {
                if (a == b) continue;
                if (seen.insert(pair_key(a, b)).second) type_edges.emplace_back(a, b);
            }
        }
    }

    for (const auto& [a, b] : type_edges) {
        arch.edges.push_back(Edge{tops[a].qname, tops[b].qname});
    }
    std::sort(arch.edges.begin(), arch.edges.end(), [](const Edge& x, const Edge& y) {
        return std::tie(x.from, x.to) < std::tie(y.from, y.to);
    });

    // Package cycles: Tarjan over the package graph (edges between different packages only).
    std::vector<std::vector<std::size_t>> adj(pkg_names.size());
    std::unordered_set<std::uint64_t> pkg_seen;
    for (const auto& [a, b] : type_edges) {
        const std::size_t pa = tops[a].pkg;
        const std::size_t pb = tops[b].pkg;
        if (pa != pb && pkg_seen.insert(pair_key(pa, pb)).second) adj[pa].push_back(pb);
    }
    std::vector<std::size_t> first_top(pkg_names.size(), kNone);
    for (std::size_t i = 0; i < tops.size(); ++i) {
        if (first_top[tops[i].pkg] == kNone) first_top[tops[i].pkg] = i;
    }
    for (const auto& comp : strongly_connected(adj)) {
        if (comp.size() < 2) continue;
        std::vector<std::string> members;
        std::size_t anchor = kNone;  // the smallest package name anchors the finding
        for (std::size_t p : comp) {
            members.push_back(display_package(pkg_names[p]));
            if (anchor == kNone || pkg_names[p] < pkg_names[anchor]) anchor = p;
        }
        std::sort(members.begin(), members.end());
        std::string list;
        for (const std::string& m : members) list += (list.empty() ? "" : ", ") + m;
        const TopType& t = tops[first_top[anchor]];
        const wb::Symbol& sym = files[t.file].report.symbols[t.sym];
        arch.findings.push_back(Finding{"package_cycle", "warning", files[t.file].path, sym.line,
                                        "packages form a cycle: " + list});
    }

    // Per-file rules.
    for (std::size_t fi = 0; fi < files.size(); ++fi) {
        const std::string& path = files[fi].path;
        const std::vector<wb::Symbol>& syms = files[fi].report.symbols;

        std::vector<std::size_t> methods_of(syms.size(), 0);
        for (const wb::Symbol& s : syms) {
            if (s.kind == wb::SymbolKind::Method && s.parent >= 0 &&
                static_cast<std::size_t>(s.parent) < syms.size()) {
                ++methods_of[static_cast<std::size_t>(s.parent)];
            }
        }
        for (std::size_t j = 0; j < syms.size(); ++j) {
            const wb::Symbol& s = syms[j];
            const bool has_parent = s.parent >= 0 && static_cast<std::size_t>(s.parent) < syms.size();
            const std::string owner = has_parent ? syms[static_cast<std::size_t>(s.parent)].name : "";
            if (s.kind == wb::SymbolKind::Class && methods_of[j] > kLargeClassMethods) {
                arch.findings.push_back(Finding{
                    "large_class", "warning", path, s.line,
                    "class " + s.name + " has " + std::to_string(methods_of[j]) + " methods (limit " +
                        std::to_string(kLargeClassMethods) + ")"});
            }
            if (s.kind == wb::SymbolKind::Field) {
                const bool in_interface =
                    has_parent && syms[static_cast<std::size_t>(s.parent)].kind == wb::SymbolKind::Interface;
                if (has_modifier(s, "public") && !has_modifier(s, "final") && !in_interface) {
                    arch.findings.push_back(Finding{
                        "public_mutable_field", "warning", path, s.line,
                        "public non-final field '" + s.name + "'" + (owner.empty() ? "" : " in " + owner)});
                }
            }
        }

        // missing_tests reuses the scaffold's rule: "<Name>Test" must be a test class name.
        for (const std::string& name : wb::missing_test_classes(syms, test_names)) {
            std::uint32_t line = 0;
            for (const wb::Symbol& s : syms) {
                if (s.parent == -1 && is_type_kind(s.kind) && s.name == name) {
                    line = s.line;
                    break;
                }
            }
            arch.findings.push_back(Finding{"missing_tests", "info", path, line,
                                            "no test class " + name + "Test"});
        }
    }

    std::sort(arch.findings.begin(), arch.findings.end(), [](const Finding& x, const Finding& y) {
        return std::tie(x.rule, x.file, x.line, x.message) < std::tie(y.rule, y.file, y.line, y.message);
    });
    return arch;
}

Architecture analyze_sources(const std::vector<SourceText>& sources,
                             const std::vector<std::string>& test_names) {
    std::vector<FileReport> files;
    wb::Session session;
    for (const SourceText& src : sources) {
        if (files.size() >= app::architect::kMaxFiles) break;
        if (src.text.size() > app::architect::kMaxFileBytes) continue;
        session.set_text(src.text);
        files.push_back(FileReport{src.path, session.analyze()});
    }
    return analyze(files, test_names);
}

std::string render_text(const Architecture& arch) {
    std::string out = "architecture: " + std::to_string(arch.files) + " files, " +
                      std::to_string(arch.types) + " types, " + std::to_string(arch.methods) +
                      " methods, " + std::to_string(arch.edges.size()) + " edges, " +
                      std::to_string(arch.findings.size()) + " findings\n";
    out += "findings:\n";
    if (arch.findings.empty()) out += "  none\n";
    for (const Finding& f : arch.findings) {
        out += "  " + f.severity + " " + f.rule + " " + f.file + ":" + std::to_string(f.line) + " " +
               f.message + "\n";
    }
    out += "edges:\n";
    if (arch.edges.empty()) out += "  none\n";
    for (const Edge& e : arch.edges) out += "  " + e.from + " -> " + e.to + "\n";
    return out;
}

}  // namespace app::architect_rules
