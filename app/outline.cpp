#include "outline.hpp"

#include <cstdint>
#include <string>

#include "imgui.h"
#include "document.hpp"
#include "editor.hpp"

namespace fs = std::filesystem;

namespace app {
namespace {

// Declarations nested deeper than this are not expanded. Java rarely nests more than a few levels.
constexpr int kMaxDepth = 16;

bool has_params(wb::SymbolKind kind) {
    return kind == wb::SymbolKind::Method || kind == wb::SymbolKind::Constructor;
}

std::string label_of(const wb::Symbol& s) {
    std::string label = s.name;
    if (has_params(s.kind)) {
        label += "(";
        for (std::size_t i = 0; i < s.params.size(); ++i) {
            if (i > 0) label += ", ";
            label += s.params[i];
        }
        label += ")";
    }
    label += "  " + std::string(wb::symbol_kind_name(s.kind)) + "  :" + std::to_string(s.line);
    return label;
}

}  // namespace

void Outline::refresh(const Document& doc) {
    if (analysed_ && path_ == doc.path() && version_ == doc.version()) return;

    session_.set_text(doc.text());
    report_ = session_.analyze();
    path_ = doc.path();
    version_ = doc.version();
    analysed_ = true;

    // Child lists from the parent indices. An index that does not point at an earlier
    // declaration is treated as top level, so bad input cannot hide entries.
    const std::size_t n = report_.symbols.size();
    children_.assign(n, {});
    roots_.clear();
    for (std::size_t i = 0; i < n; ++i) {
        const std::int32_t parent = report_.symbols[i].parent;
        if (parent < 0 || static_cast<std::size_t>(parent) >= i) {
            roots_.push_back(i);
        } else {
            children_[static_cast<std::size_t>(parent)].push_back(i);
        }
    }
}

void Outline::draw(const Document* doc, Editor& editor) {
    ImGui::TextUnformatted("Outline");
    ImGui::Separator();
    if (doc == nullptr) {
        ImGui::TextDisabled("Open a .java file to see its outline.");
        return;
    }
    if (doc->path().extension() != ".java") {
        ImGui::TextDisabled("No outline for %s", doc->name().c_str());
        return;
    }

    refresh(*doc);
    if (!report_.diagnostics.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%zu syntax error(s)",
                           report_.diagnostics.size());
    }
    if (roots_.empty()) ImGui::TextDisabled("No declarations.");

    ImGui::BeginChild("outline_tree", ImVec2(0, 0), false);
    for (std::size_t root : roots_) draw_node(root, 0, doc->path(), editor);
    ImGui::EndChild();

    if (!status_.empty()) ImGui::TextUnformatted(status_.c_str());
}

void Outline::draw_node(std::size_t index, int depth, const fs::path& path, Editor& editor) {
    const wb::Symbol& s = report_.symbols[index];
    const std::string label = label_of(s);
    ImGui::PushID(static_cast<int>(index));

    bool clicked = false;
    const bool nested = !children_[index].empty() && depth < kMaxDepth;
    if (nested) {
        // OpenOnArrow: only the arrow toggles, so a click on the label is a goto.
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen |
                                         ImGuiTreeNodeFlags_OpenOnArrow |
                                         ImGuiTreeNodeFlags_SpanAvailWidth;
        const bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
        clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
        if (open) {
            for (std::size_t child : children_[index]) draw_node(child, depth + 1, path, editor);
            ImGui::TreePop();
        }
    } else {
        clicked = ImGui::Selectable(label.c_str());
    }

    if (clicked) {
        std::string error;
        const std::size_t column = s.column > 0 ? s.column : 1;
        if (!editor.goto_line(path, s.line, column, error)) status_ = error;
    }
    ImGui::PopID();
}

}  // namespace app
