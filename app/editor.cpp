#include "editor.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <system_error>

#include "imgui.h"
#include "project.hpp"

namespace fs = std::filesystem;

namespace app {
namespace {

struct TextCallbackData {
    std::string* text;
    std::size_t* cursor;
};

// Same pattern as misc/cpp/imgui_stdlib.cpp: ImGui asks for a bigger buffer through
// CallbackResize and the std::string grows to match. CallbackAlways reports the caret.
int text_callback(ImGuiInputTextCallbackData* d) {
    auto* data = static_cast<TextCallbackData*>(d->UserData);
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        data->text->resize(static_cast<std::size_t>(d->BufTextLen));
        d->Buf = data->text->data();
    } else if (d->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        *data->cursor = static_cast<std::size_t>(d->CursorPos);
    }
    return 0;
}

// Returns true when the user edited the text. On edit, the string is trimmed to the
// NUL terminator ImGui wrote, because deletions do not trigger CallbackResize.
bool input_multiline(const char* label, std::string& text, const ImVec2& size,
                     TextCallbackData& data) {
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput |
                                      ImGuiInputTextFlags_CallbackResize |
                                      ImGuiInputTextFlags_CallbackAlways |
                                      ImGuiInputTextFlags_NoUndoRedo;  // Editor owns undo
    const bool edited = ImGui::InputTextMultiline(label, text.data(), text.capacity() + 1, size,
                                                  flags, text_callback, &data);
    if (edited) text.resize(std::strlen(text.c_str()));
    return edited;
}

}  // namespace

bool Editor::open_file(const fs::path& path, std::string& error) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(path, ec);
    if (ec) canonical = fs::absolute(path, ec);

    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].doc->path() == canonical) {
            select_next_ = i;
            return true;
        }
    }
    if (!fs::is_regular_file(canonical, ec)) {
        error = "Not a file: " + path.string();
        return false;
    }
    std::string text;
    if (!read_text_file(canonical, text, error)) return false;

    Tab tab;
    tab.doc = std::make_unique<Document>(canonical, std::move(text));
    tab.serial = next_serial_++;
    tabs_.push_back(std::move(tab));
    select_next_ = tabs_.size() - 1;
    message_ = "Opened " + canonical.filename().string();
    return true;
}

const Document* Editor::active() const {
    if (tabs_.empty()) return nullptr;
    return tabs_[std::min(active_, tabs_.size() - 1)].doc.get();
}

void Editor::draw() {
    if (tabs_.empty()) {
        ImGui::TextDisabled("Open a .java or .md file from the project tree.");
        return;
    }

    std::size_t close_index = kNone;
    if (ImGui::BeginTabBar("doc_tabs", ImGuiTabBarFlags_Reorderable |
                                           ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            Tab& tab = tabs_[i];
            // The path after "###" is the widget ID, so the dirty marker does not change it.
            const std::string label = tab.doc->name() + "###" + tab.doc->path().string();
            bool open = true;
            ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
            if (tab.doc->dirty()) flags |= ImGuiTabItemFlags_UnsavedDocument;
            if (select_next_ == i) flags |= ImGuiTabItemFlags_SetSelected;
            if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                active_ = i;
                draw_body(tab);
                ImGui::EndTabItem();
            }
            if (!open) close_index = i;
        }
        ImGui::EndTabBar();
    }
    select_next_ = kNone;

    if (close_index != kNone) {
        if (tabs_[close_index].doc->dirty()) {
            message_ = "Save " + tabs_[close_index].doc->name() + " (Ctrl+S) before closing";
        } else {
            tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(close_index));
            if (active_ >= tabs_.size() && !tabs_.empty()) active_ = tabs_.size() - 1;
            text_active_ = false;
        }
    }
}

void Editor::draw_body(Tab& tab) {
    Document& doc = *tab.doc;
    ImGuiIO& io = ImGui::GetIO();

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save(tab);
    if (text_active_ && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        undo(tab);
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float line_h = ImGui::GetTextLineHeight();
    const float pad_y = style.FramePadding.y;
    const float char_w = ImGui::CalcTextSize("0").x;
    const std::size_t lines = doc.line_count();

    int digits = 3;
    for (std::size_t n = lines; n >= 1000; n /= 10) ++digits;
    const float gutter_w = (static_cast<float>(digits) + 1.5f) * char_w;
    const float status_h = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    const float code_h = std::max(0.0f, ImGui::GetContentRegionAvail().y - status_h);

    // The text box is as tall as the whole document, so this child does the scrolling
    // and the gutter can use the child's scroll position directly.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("code", ImVec2(0, code_h), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 win_pos = ImGui::GetWindowPos();
    const float scroll_y = ImGui::GetScrollY();
    const float view_h = ImGui::GetWindowHeight();
    const float text_h = static_cast<float>(lines) * line_h + 2.0f * pad_y + line_h;

    // Scroll so the caret stays in view when it moves.
    const std::size_t caret = std::min(tab.cursor, doc.text().size());
    if (text_active_ && caret != tab.followed) {
        const float top = pad_y + static_cast<float>(doc.line_of_offset(caret)) * line_h;
        if (top < scroll_y) {
            ImGui::SetScrollY(std::max(0.0f, top - pad_y));
        } else if (top + line_h > scroll_y + view_h) {
            ImGui::SetScrollY(top + line_h + pad_y - view_h);
        }
        tab.followed = caret;
    }

    // Gutter: only the visible line numbers are drawn.
    dl->AddRectFilled(win_pos, ImVec2(win_pos.x + gutter_w, win_pos.y + view_h),
                      ImGui::GetColorU32(ImGuiCol_ChildBg, 1.0f));
    const std::size_t first = static_cast<std::size_t>(std::max(0.0f, scroll_y / line_h - 1.0f));
    const std::size_t last = std::min(lines, first + static_cast<std::size_t>(view_h / line_h) + 3);
    const ImU32 number_col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImVec2 origin = ImGui::GetCursorScreenPos();  // scroll already applied
    char label[24];
    for (std::size_t i = first; i < last; ++i) {
        std::snprintf(label, sizeof(label), "%zu", i + 1);
        const ImVec2 ts = ImGui::CalcTextSize(label);
        const ImVec2 pos(win_pos.x + gutter_w - ts.x - char_w * 0.5f,
                         origin.y + pad_y + static_cast<float>(i) * line_h);
        dl->AddText(pos, number_col, label);
    }

    ImGui::SetCursorPos(ImVec2(gutter_w, 0.0f));
    ImGui::PushID(static_cast<int>(tab.serial * 4096 + doc.revision()));
    if (refocus_) {
        ImGui::SetKeyboardFocusHere();
        refocus_ = false;
    }
    TextCallbackData data{&doc.edit_buffer(), &tab.cursor};
    if (input_multiline("##code", doc.edit_buffer(), ImVec2(-FLT_MIN, text_h), data)) {
        doc.commit_edit();
    }
    text_active_ = ImGui::IsItemActive();
    ImGui::PopID();

    // The text box is not scrollable on its own, so the wheel scrolls this view.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && io.MouseWheel != 0.0f) {
        ImGui::SetScrollY(std::max(0.0f, scroll_y - io.MouseWheel * line_h * 3.0f));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Status row: caret position, counts, save state and actions.
    const std::size_t caret_now = std::min(tab.cursor, doc.text().size());
    const std::size_t caret_line = doc.line_of_offset(caret_now);
    const std::size_t caret_col = caret_now - doc.line_start(caret_line) + 1;
    ImGui::Text("Ln %zu, Col %zu   |   %zu lines   |   %s   |   undo %zu", caret_line + 1,
                caret_col, lines, doc.dirty() ? "modified" : "saved", doc.undo_depth());
    if (ImGui::SmallButton("Save")) save(tab);
    ImGui::SameLine();
    if (ImGui::SmallButton("Undo")) undo(tab);
    ImGui::SameLine();
    ImGui::TextUnformatted(message_.c_str());
}

bool Editor::save(Tab& tab) {
    std::string error;
    if (!write_text_file(tab.doc->path(), tab.doc->text(), error)) {
        message_ = error;
        return false;
    }
    tab.doc->mark_saved();
    message_ = "Saved " + tab.doc->name();
    return true;
}

void Editor::undo(Tab& tab) {
    if (!tab.doc->undo()) {
        message_ = "Nothing to undo";
        return;
    }
    refocus_ = text_active_;  // keep typing focus after the widget reloads
    message_ = "Undo";
}

}  // namespace app
