#include "project.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace app {
namespace {

// Directories that are never listed: build outputs, VCS and tool state, hidden folders.
bool skip_directory(const std::string& name) {
    if (name.empty() || name[0] == '.') return true;
    if (name.rfind("build", 0) == 0) return true;  // build, build-wasm, build-debug, ...
    return false;
}

bool listed_file(const fs::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".java" || ext == ".md";
}

struct Walk {
    std::size_t entries = 0;
    std::size_t max_entries = 0;
    bool truncated = false;
};

// Recursive walk. Symlinks are not followed, which avoids cycles.
void walk(const fs::path& dir, TreeNode& node, Walk& state) {
    std::error_code ec;
    std::vector<fs::directory_entry> items;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) return;
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        items.push_back(*it);
    }

    std::vector<std::pair<bool, fs::directory_entry>> keyed;  // (is_file, entry)
    keyed.reserve(items.size());
    for (auto& e : items) {
        std::error_code sec;
        if (e.is_symlink(sec)) continue;
        const bool is_dir = e.is_directory(sec);
        const bool is_file = !is_dir && e.is_regular_file(sec);
        if (!is_dir && !is_file) continue;
        keyed.emplace_back(!is_dir, e);
    }
    std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return !a.first;  // directories first
        return a.second.path().filename().string() < b.second.path().filename().string();
    });

    for (const auto& [is_file, e] : keyed) {
        if (state.entries >= state.max_entries) {
            state.truncated = true;
            return;
        }
        const std::string name = e.path().filename().string();
        if (!is_file) {
            if (skip_directory(name)) continue;
            ++state.entries;
            TreeNode child;
            child.name = name;
            child.path = e.path();
            child.is_dir = true;
            walk(e.path(), child, state);
            node.children.push_back(std::move(child));
        } else {
            if (!listed_file(e.path())) continue;
            ++state.entries;
            TreeNode child;
            child.name = name;
            child.path = e.path();
            node.children.push_back(std::move(child));
        }
        if (state.truncated) return;
    }
}

}  // namespace

bool ProjectTree::open(const fs::path& folder, std::string& error) {
    std::error_code ec;
    fs::path root = fs::absolute(folder, ec);
    if (!ec) root = fs::weakly_canonical(root, ec);
    if (ec || !fs::is_directory(root, ec)) {
        error = "Not a folder: " + folder.string();
        return false;
    }

    // Skip rules apply to descendants only, so opening "." or a hidden folder still works.
    root_ = TreeNode{};
    root_.name = root.filename().empty() ? root.string() : root.filename().string();
    root_.path = root;
    root_.is_dir = true;

    Walk state;
    state.max_entries = kMaxEntries;
    walk(root, root_, state);
    entries_ = state.entries;
    truncated_ = state.truncated;
    open_ = true;
    return true;
}

bool read_text_file(const fs::path& path, std::string& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Cannot read " + path.string();
        return false;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

bool write_text_file(const fs::path& path, const std::string& text, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Cannot write " + path.string();
        return false;
    }
    out << text;
    out.flush();
    if (!out) {
        error = "Write failed for " + path.string();
        return false;
    }
    return true;
}

}  // namespace app
