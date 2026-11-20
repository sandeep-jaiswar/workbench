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

The right-hand panel is a chat console. Type a command or a question and press Enter.

- `/patterns` lists the catalog patterns.
- `/generate <pattern> [Class]` generates a pattern and shows the code only if it passes the checks; otherwise the reasons are shown.
- `/check` runs the syntax check on the active file.
- `/name <Class>` sets the class name used by `/generate` and the Task trace.
- `/clear` clears the conversation; `/help` lists the commands.
- Any other text goes to the local model (java-wasm's llm module) with the recent conversation as context. Without a model it says so and only the commands work.

The Task trace button is in the chat header. The chat and the debug step share the model, so they take turns.

## Status

- Done: builds against the pinned java-wasm core; starts and runs under Xvfb.
- Not yet tested: clicking the buttons in the window.
- Done: the debug step uses a local model. The default is Qwen2.5-Coder-7B-Instruct (Apache 2.0).
  Fetch it with `scripts/fetch-model.sh <dir>` from java-wasm, then build with
  `-DWB_MODEL_DIR=<dir>`, or set `WB_MODEL=/path/to/model.gguf` at run time. With no model file the
  debug step is skipped.
- A proposal can change several project files. Each file's edit is checked against its text on
  disk. The whole set is staged (javac and tests on a copy) and applied only as one unit, after the
  user confirms. A conflict in any file writes nothing.
- Each proposal is staged before the debug step accepts it. A rejected proposal goes back to the
  model with the reason and the failing assertions, up to 3 attempts.
- Not included: model weights (fetched separately, see above).

## License

MIT. See `LICENSE` and `THIRD_PARTY.md`.
