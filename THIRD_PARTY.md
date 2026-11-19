# Third-party components

| Component | Version / commit | License | How it is used |
|---|---|---|---|
| tree-sitter runtime (C) | npm `tree-sitter` 0.25.0, vendored under `third_party/tree-sitter` | MIT | Compiled into `tree_sitter` |
| tree-sitter-java grammar | npm `tree-sitter-java` 0.23.5, vendored under `third_party/tree-sitter-java` | MIT | `parser.c` compiled into `tree_sitter_java` |
| nlohmann/json | `098eebd4` (fetched) | MIT | Catalog parsing |
| Dear ImGui | `77fe4588` (fetched) | MIT | Desktop UI (`WORKBENCH_BUILD_APP`) |
| GLFW | `92dcf4ce` (fetched) | Zlib | Window and input (`WORKBENCH_BUILD_APP`) |

Vendored sources keep their original license files where the upstream package provided them.
