#include "document.hpp"

#include <algorithm>
#include <utility>

namespace app {

Document::Document(std::filesystem::path path, std::string text)
    : path_(std::move(path)), text_(std::move(text)), committed_(text_), saved_(text_) {
    refresh();
}

std::size_t Document::line_of_offset(std::size_t offset) const {
    // First line start strictly greater than offset, minus one.
    auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
    return static_cast<std::size_t>(it - line_starts_.begin()) - 1;
}

std::size_t Document::offset_of(std::size_t line, std::size_t column) const {
    // 1-based line and column in, 0-based line index for the cache lookup.
    const std::size_t last_line = line_starts_.size() - 1;
    const std::size_t index = line == 0 ? 0 : std::min(line - 1, last_line);
    const std::size_t start = line_starts_[index];
    const std::size_t end = index == last_line ? text_.size() : line_starts_[index + 1] - 1;
    const std::size_t col = column == 0 ? 1 : column;
    return std::min(start + (col - 1), end);
}

void Document::commit_edit() {
    if (text_ == committed_) return;
    undo_.push_back(std::move(committed_));
    if (undo_.size() > kUndoLimit) undo_.pop_front();
    committed_ = text_;
    refresh();
}

void Document::set_text(std::string text) {
    text_ = std::move(text);
    commit_edit();
}

bool Document::undo() {
    if (undo_.empty()) return false;
    text_ = std::move(undo_.back());
    undo_.pop_back();
    committed_ = text_;
    ++revision_;
    refresh();
    return true;
}

void Document::mark_saved() {
    saved_ = text_;
    dirty_ = false;
}

void Document::refresh() {
    ++version_;
    line_starts_.clear();
    line_starts_.push_back(0);
    for (std::size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') line_starts_.push_back(i + 1);
    }
    dirty_ = text_ != saved_;
}

}  // namespace app
