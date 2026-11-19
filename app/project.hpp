#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace app {

// One entry in the project tree. Only directories and .java / .md files are stored.
struct TreeNode {
    std::string name;
    std::filesystem::path path;
    bool is_dir = false;
    std::vector<TreeNode> children;  // directories first, then by name
};

// A folder walked once with std::filesystem. Holds no UI state.
class ProjectTree {
public:
    static constexpr std::size_t kMaxEntries = 5000;

    // Walks `folder`. Returns false and sets `error` if it is not a readable directory.
    bool open(const std::filesystem::path& folder, std::string& error);

    bool is_open() const { return open_; }
    const TreeNode& root() const { return root_; }
    std::size_t entries() const { return entries_; }
    bool truncated() const { return truncated_; }

private:
    bool open_ = false;
    TreeNode root_;
    std::size_t entries_ = 0;
    bool truncated_ = false;
};

// Whole-file read and write. Both set `error` and return false on failure.
bool read_text_file(const std::filesystem::path& path, std::string& out, std::string& error);
bool write_text_file(const std::filesystem::path& path, const std::string& text, std::string& error);

}  // namespace app
