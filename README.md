# Workbench

A native desktop app for verified Java design-pattern generation, built with Dear ImGui.
The verifier, engine, and catalog come from [java-wasm](https://github.com/sandeep-jaiswar/java-wasm),
so the desktop app and the browser build share one implementation.

## Layout

```
app/main.cpp          Dear ImGui + GLFW + OpenGL3 front end
CMakeLists.txt        Fetches java-wasm (core), Dear ImGui, and GLFW at pinned commits
THIRD_PARTY.md        Third-party licenses
```

The core (`wb_core`) and the catalog come from java-wasm. Nothing is duplicated here.

## Build

Requirements: CMake 3.20+, a C++17 compiler, git, and on Linux the X11 and OpenGL dev packages
(`libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/workbench                       # uses the catalog from java-wasm
./build/workbench /path/to/patterns.json  # or pass your own catalog
```

To develop against a local checkout of java-wasm:

```bash
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_JAVA_WASM=/path/to/java-wasm
```

GLFW is built with the X11 backend only. For Wayland, install `wayland-scanner` and change
`GLFW_BUILD_WAYLAND`/`GLFW_BUILD_X11` in `CMakeLists.txt`.

## Using the app

- **Generate:** pick a pattern, enter a class name, and click Generate. The code is shown only if it passes the checks; otherwise the reasons are shown.
- **Check:** paste any Java source and click Check syntax to see the first error line.

## Status

- Done: builds against the pinned java-wasm core; starts and runs under Xvfb.
- Not yet tested: clicking the buttons in the window.
- Not included: model weights and the llama.cpp sampler (planned; see java-wasm README).

## License

MIT. See `LICENSE` and `THIRD_PARTY.md`.
