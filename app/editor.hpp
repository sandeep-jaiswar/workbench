#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "document.hpp"

namespace app {

// Tabbed multi-line editor for the open files. Owns the Documents and the UI state
// (caret position, tab selection). All drawing happens in draw().
class Editor {
public:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    // Opens a file in a new tab, or selects its tab when it is already open.
    bool open_file(const std::filesystem::path& path, std::string& error);

    // Draws the tab bar and the active file into the current window.
    void draw();

    // Opens the file (or selects its tab) and moves the caret to a 1-based line and column.
    // The caret moves and the line is scrolled into view on the next draw of that tab, so
    // the call is cheap and safe from any panel. Returns false when the file cannot be opened.
    bool goto_line(const std::filesystem::path& path, std::size_t line, std::size_t column,
                   std::string& error);

    // The tab that is shown, or nullptr when no file is open.
    const Document* active() const;

    // Short status text shown under the editor (save/undo/open results).
    const std::string& message() const { return message_; }

private:
    struct Tab {
        std::unique_ptr<Document> doc;
        std::size_t serial = 0;  // stable per tab; part of the widget ID
        std::size_t cursor = 0;  // byte offset of the caret, updated by the text widget
        std::size_t followed = kNone;  // caret offset the view last scrolled to
        std::size_t pending_caret = kNone;  // caret to apply inside the text callback
        float scroll_to = 0.0f;       // y of the target line, while scroll_frames > 0
        int scroll_frames = 0;        // scroll is re-issued for a few frames (see draw_body)
    };

    void draw_body(Tab& tab);
    bool save(Tab& tab);
    void undo(Tab& tab);

    std::vector<Tab> tabs_;
    std::size_t active_ = 0;
    std::size_t select_next_ = kNone;  // tab to select on the next frame
    std::size_t next_serial_ = 1;
    bool text_active_ = false;  // the text widget had focus at the end of last frame
    bool refocus_ = false;      // re-focus the text widget after an undo reloads it
    std::size_t goto_serial_ = kNone;  // tab with a pending goto_line, by serial
    std::size_t goto_line_ = 0;        // 1-based
    std::size_t goto_column_ = 1;      // 1-based
    std::string message_;
};

}  // namespace app
