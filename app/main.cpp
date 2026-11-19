// Workbench: Dear ImGui desktop front end for the java-wasm pipeline.
#include <cfloat>
#include <cstdio>
#include <exception>
#include <string>

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "catalog.hpp"
#include "engine.hpp"
#include "verify.hpp"

#ifndef WB_DEFAULT_CATALOG
#define WB_DEFAULT_CATALOG "catalog/patterns.json"
#endif

namespace {

void glfw_error(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string catalog_path = argc > 1 ? argv[1] : WB_DEFAULT_CATALOG;

    wb::Catalog catalog;
    wb::Verifier verifier;
    try {
        catalog = wb::Catalog::load(catalog_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }

    glfwSetErrorCallback(glfw_error);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1280, 760, "Workbench", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    int selected = 0;
    char name[64] = "Demo";
    std::string output;
    std::string status;
    std::string check_result;
    static char java_source[16384] =
        "public class Hello {\n    public static void main(String[] args) {\n"
        "        System.out.println(\"hi\");\n    }\n}\n";

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("Workbench", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);

        ImGui::BeginChild("generate", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 6, 0), true);
        ImGui::TextUnformatted("Generate a pattern");
        ImGui::Separator();
        const auto& patterns = catalog.patterns();
        if (ImGui::BeginCombo("Pattern", patterns[selected].name.c_str())) {
            for (int i = 0; i < static_cast<int>(patterns.size()); ++i) {
                if (ImGui::Selectable(patterns[i].name.c_str(), i == selected)) selected = i;
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("Class name", name, sizeof(name));
        if (ImGui::Button("Generate and verify")) {
            try {
                const auto r = wb::generate(verifier, patterns[selected], {{"Name", name}});
                if (r.ok) {
                    output = r.source;
                    status = "Verified";
                } else {
                    output.clear();
                    status = "Rejected: ";
                    for (const auto& why : r.rejections) status += why + " | ";
                }
            } catch (const std::exception& e) {
                output.clear();
                status = std::string("Error: ") + e.what();
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(status.rfind("Verified", 0) == 0 ? ImVec4(0.3f, 0.9f, 0.4f, 1)
                                                            : ImVec4(0.95f, 0.45f, 0.45f, 1),
                           "%s", status.c_str());
        ImGui::InputTextMultiline("##output", output.data(), output.size() + 1,
                                  ImVec2(-FLT_MIN, -FLT_MIN),
                                  ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("check", ImVec2(0, 0), true);
        ImGui::TextUnformatted("Check your Java");
        ImGui::Separator();
        ImGui::InputTextMultiline("##java", java_source, sizeof(java_source),
                                  ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y - 60),
                                  ImGuiInputTextFlags_AllowTabInput);
        if (ImGui::Button("Check syntax")) {
            const auto a = verifier.analyze(java_source);
            check_result = a.ok ? "No syntax errors"
                                : std::to_string(a.errors.size()) + " syntax error(s), first at line " +
                                      std::to_string(a.errors.front().line);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(check_result.c_str());
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();
        int fb_w = 0, fb_h = 0;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.10f, 0.10f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
