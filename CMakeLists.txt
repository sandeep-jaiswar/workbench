cmake_minimum_required(VERSION 3.20)
project(workbench LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---- core: the C++ verifier and engine from java-wasm (MIT) ----------------
# For local development, point at a checkout:
#   cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_JAVA_WASM=/path/to/java-wasm
set(WB_BUILD_TESTS OFF CACHE BOOL "" FORCE)  # java-wasm owns the core tests
set(WB_LLM ON CACHE BOOL "" FORCE)           # native local model (llama.cpp) for the fixer

# GPU for the local model. On by default when the CUDA compiler is found; set -DWB_CUDA=OFF for CPU.
# 20 of the 28 layers of the 7B model fit in 4 GB of VRAM (measured on a GTX 1650 Max-Q). The
# architecture default is for that card (sm_75); override with -DCMAKE_CUDA_ARCHITECTURES=...
include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
    set(_wb_cuda_default ON)
else()
    set(_wb_cuda_default OFF)
endif()
option(WB_CUDA "Run the local model on the GPU when the CUDA toolkit is found" ${_wb_cuda_default})
if(WB_CUDA)
    enable_language(CUDA)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 75)
    endif()
    set(WB_LLM_CUDA ON CACHE BOOL "" FORCE)  # read by java-wasm's llama.cpp fetch
    set(WB_LLM_GPU_LAYERS 20 CACHE STRING "Layers offloaded to the GPU (7B model, 4 GB card)")
else()
    set(WB_LLM_CUDA OFF CACHE BOOL "" FORCE)
    set(WB_LLM_GPU_LAYERS 0 CACHE STRING "Layers offloaded to the GPU (0 = CPU only)")
endif()
FetchContent_Declare(java_wasm
    GIT_REPOSITORY https://github.com/sandeep-jaiswar/java-wasm.git
    GIT_TAG        9854b8dd2eef28c6202c0672d749aeedb4d401f4)
FetchContent_MakeAvailable(java_wasm)

# Where the default local model lives. Install it with java-wasm's scripts/fetch-model.sh <dir>.
set(WB_MODEL_DIR "${CMAKE_SOURCE_DIR}/models" CACHE PATH "Directory holding the default GGUF model")

# ---- third-party: Dear ImGui, GLFW (fetched at pinned commits) -------------
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

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
add_executable(workbench
    app/main.cpp
    app/app_window.cpp
    app/document.cpp
    app/editor.cpp
    app/generate_panel.cpp
    app/architect.cpp
    app/architect_rules.cpp
    app/apply_fix.cpp
    app/fixer.cpp
    app/llm_fixer.cpp
    app/java_tools.cpp
    app/output.cpp
    app/outline.cpp
    app/project.cpp
    app/project_panel.cpp
    app/task_trace.cpp
    app/test_gen.cpp
    app/trace_panel.cpp)
target_include_directories(workbench PRIVATE app ${java_wasm_SOURCE_DIR}/src/graph)
find_package(Threads REQUIRED)
target_link_libraries(workbench PRIVATE wb_core wb_llm imgui Threads::Threads)
# The catalog lives in java-wasm; the app defaults to that copy.
target_compile_definitions(workbench PRIVATE
    WB_DEFAULT_CATALOG="${java_wasm_SOURCE_DIR}/catalog/patterns.json"
    WB_DEFAULT_MODEL="${WB_MODEL_DIR}/qwen2.5-coder-7b-instruct-q4_k_m.gguf"
    WB_LLM_GPU_LAYERS=${WB_LLM_GPU_LAYERS})

# ---- tests ------------------------------------------------------------------
# Headless: Document only, no ImGui, GLFW or display.
enable_testing()
add_executable(document_test tests/document_test.cpp app/document.cpp)
target_include_directories(document_test PRIVATE app)
add_test(NAME document_test COMMAND document_test)

add_executable(java_tools_test tests/java_tools_test.cpp app/java_tools.cpp)
target_include_directories(java_tools_test PRIVATE app)
add_test(NAME java_tools_test COMMAND java_tools_test)

# Trace panel's task runner, headless: wb_core only, no ImGui or GLFW.
add_executable(trace_panel_test tests/trace_panel_test.cpp app/task_trace.cpp app/fixer.cpp app/architect.cpp
               app/architect_rules.cpp app/java_tools.cpp)
target_include_directories(trace_panel_test PRIVATE app ${java_wasm_SOURCE_DIR}/src/graph)
target_link_libraries(trace_panel_test PRIVATE wb_core Threads::Threads)
add_test(NAME trace_panel COMMAND trace_panel_test ${java_wasm_SOURCE_DIR}/catalog/patterns.json)

# "Tests for missing classes": headless, wb_core only.
add_executable(test_gen_test tests/test_gen_test.cpp app/test_gen.cpp app/architect.cpp app/architect_rules.cpp)
target_include_directories(test_gen_test PRIVATE app)
target_link_libraries(test_gen_test PRIVATE wb_core)
add_test(NAME test_gen COMMAND test_gen_test)
# Architecture graph and rules: pure, wb_core only, no ImGui or GLFW.
add_executable(architect_rules_test tests/architect_rules_test.cpp app/architect_rules.cpp)
target_include_directories(architect_rules_test PRIVATE app)
target_link_libraries(architect_rules_test PRIVATE wb_core)
add_test(NAME architect_rules COMMAND architect_rules_test)
# Fix proposals: class-path mapping, Myers diff, and the stage/apply protocol on temp copies.
add_executable(apply_fix_test tests/apply_fix_test.cpp app/apply_fix.cpp app/fixer.cpp
               app/java_tools.cpp)
target_include_directories(apply_fix_test PRIVATE app)
target_link_libraries(apply_fix_test PRIVATE wb_core Threads::Threads)
add_test(NAME apply_fix_test COMMAND apply_fix_test)

# LLM fixer: prompt, reply parsing and error paths. Pass a GGUF path to also run one real
# completion: cmake -DWB_TEST_MODEL=/path/to/model.gguf (the real run is skipped when empty).
set(WB_TEST_MODEL "" CACHE FILEPATH "GGUF model for llm_fixer_test (optional)")
add_executable(llm_fixer_test tests/llm_fixer_test.cpp app/llm_fixer.cpp app/fixer.cpp)
target_include_directories(llm_fixer_test PRIVATE app)
target_link_libraries(llm_fixer_test PRIVATE wb_llm)
add_test(NAME llm_fixer COMMAND llm_fixer_test ${WB_TEST_MODEL})
