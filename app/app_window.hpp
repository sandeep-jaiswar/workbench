#pragma once

#include <functional>

struct GLFWwindow;

namespace app {

// Owns the GLFW window and the Dear ImGui context, and runs the frame loop.
class AppWindow {
public:
    AppWindow() = default;
    ~AppWindow();
    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    // Creates the window and the ImGui context. Returns false on failure.
    bool init(int width, int height, const char* title);

    // Runs until the window is closed. `draw` builds one ImGui frame.
    void run(const std::function<void()>& draw);

private:
    GLFWwindow* window_ = nullptr;
};

}  // namespace app
