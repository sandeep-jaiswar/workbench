#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "project.hpp"

namespace app {

// State of the project sidebar: the folder field and the walked tree.
struct ProjectPanel {
    ProjectPanel();  // the folder field starts at the current working directory

    // Walks the folder in the field and updates `status` with the result.
    void open_folder();

    char folder[512] = {};
    ProjectTree tree;
    std::string status;
};

// Draws the folder field, the Open button and the tree. `on_open` runs for each file
// clicked in the tree.
void draw_project_panel(ProjectPanel& panel,
                        const std::function<void(const std::filesystem::path&)>& on_open);

}  // namespace app
