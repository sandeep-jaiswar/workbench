// Workbench: Dear ImGui desktop front end for the java-wasm pipeline.
// Entry point and root layout. Window and frame loop: app_window.cpp. Panels: *_panel, editor,
// output and generate_panel sources.
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

#include "imgui.h"

#include "app_window.hpp"
#include "catalog.hpp"
#include "editor.hpp"
#include "generate_panel.hpp"
#include "output.hpp"
#include "project_panel.hpp"
#include "verify.hpp"

#ifndef WB_DEFAULT_CATALOG
#define WB_DEFAULT_CATALOG "catalog/patterns.json"
#endif

namespace {

constexpr float kProjectWidth = 260.0f;
constexpr float kGenerateWidth = 360.0f;
constexpr float kOutputHeight = 190.0f;

}  // namespace

int main(int argc, char** argv) {
    const std::string catalog_path = argc > 1 ? argv[1] : WB_DEFAULT_CATALOG;

    wb::Catalog catalog;
    try {
        catalog = wb::Catalog::load(catalog_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }

    app::AppWindow window;
    if (!window.init(1280, 760, "Workbench")) return 1;

    wb::Verifier verifier;
    app::ProjectPanel project;
    app::Editor editor;
    app::GenerateState generate;
    app::CheckState check;
    project.open_folder();

    window.run([&] {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("Workbench", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float center_w = avail.x - kProjectWidth - kGenerateWidth - 2.0f * spacing;

        ImGui::BeginChild("project", ImVec2(kProjectWidth, 0), true);
        app::draw_project_panel(project, [&](const std::filesystem::path& file) {
            std::string error;
            if (!editor.open_file(file, error)) project.status = error;
        });
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("center", ImVec2(center_w, 0), false);
        ImGui::BeginChild("editor", ImVec2(0, avail.y - kOutputHeight - spacing), true);
        editor.draw();
        ImGui::EndChild();
        ImGui::BeginChild("output", ImVec2(0, 0), true);
        app::draw_output_panel(check, verifier, editor.active());
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("generate", ImVec2(kGenerateWidth, 0), true);
        app::draw_generate_panel(generate, catalog, verifier);
        ImGui::EndChild();

        ImGui::End();
    });
    return 0;
}
