# Workbench

A native desktop app for verified Java design-pattern generation, built with Dear ImGui.
It shares its catalog and verification rules with `java-wasm`, the browser version.

## Layout

```
catalog/patterns.json      Pattern templates (same file as java-wasm)
src/core/                  Core library, no GUI dependencies
  catalog.*                Loads the JSON catalog; O(1) lookup by id
  verify.*                 tree-sitter Java verifier; one iterative O(n) pass
  engine.*                 Template fill and best-of-N with early exit
app/main.cpp               Dear ImGui + GLFW + OpenGL3 front end
tests/test_core.cpp        Core tests (20 checks)
third_party/               Vendored tree-sitter runtime and Java grammar
```

## Build

Requirements: CMake 3.20+, a C++17 compiler, git (for FetchContent), and on Linux the
X11 and OpenGL development packages (`libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure   # core tests, no display needed
./build/workbench catalog/patterns.json      # desktop app
```

Options:

- `WORKBENCH_BUILD_APP` (ON): build the ImGui app. Turn it off on a headless machine.
- `WORKBENCH_BUILD_TESTS` (ON): build the core tests.

GLFW is built with the X11 backend only. To use Wayland, install `wayland-scanner` and change
`GLFW_BUILD_WAYLAND`/`GLFW_BUILD_X11` in `CMakeLists.txt`.

## Using the app

- **Generate:** pick a pattern, enter a class name, and click Generate. The result is shown
  only if it passes the syntax and structure checks. Otherwise the reasons are shown.
- **Check:** paste any Java source and click Check syntax to see the first error line.

## Design notes

- The verifier reuses one tree-sitter parser per process, so repeated checks skip parser setup.
- Each parse tree is freed after use.
- Requirement checks are O(1) per name, using hash sets built during the single tree pass.

## Status

- Working: core library, verifier, engine, 20 passing checks, the desktop app (starts and runs
  under Xvfb).
- Not included: a neural sampler. The `Sampler` hook in `engine.hpp` is ready for llama.cpp
  (MIT) with MIT model weights, which are not bundled.
- Not yet tested: clicking the buttons in the window. Only the startup was verified.
- Limit: the verifier checks syntax and declared structure, not types. Add a `javac` step for
  full compile checks. On desktop, you can run `javac` as a subprocess.

## License

MIT. See `LICENSE` and `THIRD_PARTY.md`.
