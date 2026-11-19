#include "app_window.hpp"

#include <cstdio>

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace app {
namespace {

void glfw_error(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

}  // namespace

AppWindow::~AppWindow() {
    if (!window_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();
}

bool AppWindow::init(int width, int height, const char* title) {
    glfwSetErrorCallback(glfw_error);
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    return true;
}

void AppWindow::run(const std::function<void()>& draw) {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        draw();

        ImGui::Render();
        int fb_w = 0, fb_h = 0;
        glfwGetFramebufferSize(window_, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.10f, 0.10f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }
}

}  // namespace app
