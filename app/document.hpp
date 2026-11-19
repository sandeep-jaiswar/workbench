#pragma once

#include <cstddef>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace app {

// One open text file: path, text, dirty flag, cached line starts and an undo stack.
// Holds no UI state and does no file IO, so it can be tested without a display.
//
// Editing protocol: the editor lets ImGui write into edit_buffer() while a text widget
// is active, then calls commit_edit() whenever the widget reports an edit.
class Document {
public:
    static constexpr std::size_t kUndoLimit = 50;

    Document(std::filesystem::path path, std::string text);

    const std::filesystem::path& path() const { return path_; }
    std::string name() const { return path_.filename().string(); }
    const std::string& text() const { return text_; }
    bool dirty() const { return dirty_; }
    std::size_t undo_depth() const { return undo_.size(); }
    // Bumped whenever the text changes outside the widget (undo), so the editor can reload it.
    std::size_t revision() const { return revision_; }
    // Bumped on every change to the text (typing, set_text, undo). Use it to detect that
    // derived data such as the outline must be recomputed. Unlike revision(), it does not
    // change the widget ID.
    std::size_t version() const { return version_; }

    // Line queries use the cache. A text with N '\n' characters has N + 1 lines, so
    // empty text has one line.
    std::size_t line_count() const { return line_starts_.size(); }
    std::size_t line_start(std::size_t line) const { return line_starts_[line]; }
    // 0-based line containing byte `offset` (binary search over the cache).
    std::size_t line_of_offset(std::size_t offset) const;
    // Byte offset of a position. `line` is 1-based and `column` is 1-based bytes, the same
    // convention as wb::Symbol and javac. Out-of-range values are clamped: a line past the
    // end goes to the last line, a column past the end of its line stops before the '\n',
    // and 0 is treated as 1. O(1) on the line cache.
    std::size_t offset_of(std::size_t line, std::size_t column) const;

    // Mutable buffer for the text widget. Call commit_edit() after it changes.
    std::string& edit_buffer() { return text_; }
    // Records the pre-edit snapshot on the undo stack, then refreshes caches and dirty flag.
    void commit_edit();

    // Replaces the whole text as one undoable edit.
    void set_text(std::string text);
    // Restores the previous snapshot. Returns false when the undo stack is empty.
    bool undo();
    // Call after the text was written to disk successfully.
    void mark_saved();

private:
    void refresh();

    std::filesystem::path path_;
    std::string text_;
    std::string committed_;  // text as of the last commit; becomes the next undo snapshot
    std::string saved_;      // text as of the last successful save (or open)
    std::deque<std::string> undo_;
    std::vector<std::size_t> line_starts_;
    bool dirty_ = false;
    std::size_t revision_ = 0;
    std::size_t version_ = 0;
};

}  // namespace app
