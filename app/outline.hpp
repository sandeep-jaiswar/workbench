#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "session.hpp"
#include "symbols.hpp"

namespace app {

class Document;
class Editor;

// Outline of the active Java document: classes, interfaces, records, methods and fields as a
// tree with line numbers. The file is parsed only when its path or text version changes, not
// on every frame. Clicking an entry moves the editor caret to its declaration.
class Outline {
public:
    // Draws into the current window. `doc` may be nullptr (no file open).
    void draw(const Document* doc, Editor& editor);

private:
    void refresh(const Document& doc);
    void draw_node(std::size_t index, int depth, const std::filesystem::path& path, Editor& editor);

    wb::Session session_;
    std::filesystem::path path_;
    std::size_t version_ = 0;
    bool analysed_ = false;
    wb::Report report_;
    std::vector<std::vector<std::size_t>> children_;  // indices into report_.symbols
    std::vector<std::size_t> roots_;
    std::string status_;
};

}  // namespace app
