cmake_minimum_required(VERSION 3.20)
project(workbench LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

option(WORKBENCH_BUILD_APP "Build the Dear ImGui desktop app" ON)
option(WORKBENCH_BUILD_TESTS "Build the core tests" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---- third-party: tree-sitter runtime + Java grammar (vendored, MIT) -------
add_library(tree_sitter STATIC third_party/tree-sitter/lib/src/lib.c)
target_include_directories(tree_sitter
    PUBLIC third_party/tree-sitter/lib/include
    PRIVATE third_party/tree-sitter/lib/src)

add_library(tree_sitter_java STATIC third_party/tree-sitter-java/src/parser.c)
target_include_directories(tree_sitter_java PUBLIC third_party/tree-sitter-java/src)

# ---- third-party: nlohmann/json (MIT), fetched at a pinned commit ---------
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        098eebd412883ccb9d1a26523758e5ec457f58e0)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# ---- core library: no GUI dependencies ------------------------------------
add_library(wb_core STATIC
    src/core/catalog.cpp
    src/core/verify.cpp
    src/core/engine.cpp)
target_include_directories(wb_core PUBLIC src/core)
target_link_libraries(wb_core PUBLIC tree_sitter tree_sitter_java nlohmann_json::nlohmann_json)

# ---- tests ------------------------------------------------------------------
if(WORKBENCH_BUILD_TESTS)
    enable_testing()
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE wb_core)
    add_test(NAME core COMMAND test_core ${CMAKE_SOURCE_DIR}/catalog/patterns.json)
endif()

# ---- desktop app: Dear ImGui + GLFW + OpenGL3 -------------------------------
if(WORKBENCH_BUILD_APP)
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

    # Dear ImGui ships no CMakeLists.txt, so fetch the sources and build them below.
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

    add_executable(workbench app/main.cpp)
    target_link_libraries(workbench PRIVATE wb_core imgui)
endif()
