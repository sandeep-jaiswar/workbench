cmake_minimum_required(VERSION 3.20)
project(workbench LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---- core: the C++ verifier and engine from java-wasm (MIT) ----------------
# For local development, point at a checkout:
#   cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_JAVA_WASM=/path/to/java-wasm
set(WB_BUILD_TESTS OFF CACHE BOOL "" FORCE)  # java-wasm owns the core tests
FetchContent_Declare(java_wasm
    GIT_REPOSITORY https://github.com/sandeep-jaiswar/java-wasm.git
    GIT_TAG        13df0e762353af988163511d7cc1bc507267ca68)
FetchContent_MakeAvailable(java_wasm)

# ---- third-party: Dear ImGui, GLFW (fetched at pinned commits) -------------
find_package(OpenGL REQUIRED)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        92dcf4ce74f2e2554a98fea09be7c705c17daa5a)
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
set(GLFW_BUILD_WAYLAND OFF CACHE INTERNAL "")
set(GLFW_BUILD_X11 ON CACHE INTERNAL "")
FetchContent_MakeAvailable(glfw)

# Dear ImGui ships no CMakeLists.txt, so fetch the sources and build them here.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        77fe4588e6af934b4167b91312174ca3fbb0d71a)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# ---- desktop app ------------------------------------------------------------
add_executable(workbench app/main.cpp)
target_link_libraries(workbench PRIVATE wb_core imgui)
# The catalog lives in java-wasm; the app defaults to that copy.
target_compile_definitions(workbench PRIVATE
    WB_DEFAULT_CATALOG="${java_wasm_SOURCE_DIR}/catalog/patterns.json")
