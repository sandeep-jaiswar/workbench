#include "project_panel.hpp"

#include <cfloat>
#include <cstdio>
#include <string>
#include <system_error>

#include "imgui.h"

namespace fs = std::filesystem;

namespace app {
namespace {

void draw_node(const TreeNode& node,
               const std::function<void(const fs::path&)>& on_open) {
    // The path is the widget ID, so names that repeat in different folders stay distinct.
    ImGui::PushID(node.path.string().c_str());
    if (node.is_dir) {
        if (ImGui::TreeNode(node.name.c_str())) {
            for (const TreeNode& child : node.children) draw_node(child, on_open);
            ImGui::TreePop();
        }
    } else {
        if (ImGui::Selectable(node.name.c_str())) on_open(node.path);
    }
    ImGui::PopID();
}

}  // namespace

ProjectPanel::ProjectPanel() {
    std::error_code ec;
    const std::string cwd = fs::current_path(ec).string();
    std::snprintf(folder, sizeof(folder), "%s", cwd.c_str());
}

void ProjectPanel::open_folder() {
    std::string error;
    if (tree.open(fs::path(folder), error)) {
        status = std::to_string(tree.entries()) + " entries";
    } else {
        status = error;
    }
}

void draw_project_panel(ProjectPanel& panel,
                        const std::function<void(const fs::path&)>& on_open) {
    ImGui::TextUnformatted("Project");
    ImGui::Separator();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##folder", panel.folder, sizeof(panel.folder));
    if (ImGui::Button("Open folder")) panel.open_folder();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", panel.status.c_str());
    if (panel.tree.is_open()) {
        ImGui::BeginChild("tree", ImVec2(0, 0), false);
        for (const TreeNode& child : panel.tree.root().children) draw_node(child, on_open);
        if (panel.tree.truncated()) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1), "Listing capped at %zu entries.",
                               ProjectTree::kMaxEntries);
        }
        ImGui::EndChild();
    }
}

}  // namespace app
