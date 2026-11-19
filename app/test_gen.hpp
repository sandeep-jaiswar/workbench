#pragma once

// "Tests for missing classes": finds the top-level types under <project>/src that have no
// "<Name>Test" class under <project>/test, and writes a stub test file for each one. The
// logic is headless (wb_core only, no ImGui, no GLFW). Existing files are never overwritten.

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "symbols.hpp"

namespace wb {
class Session;
}

namespace app::test_gen {

// One parsed .java file under src/. `symbols` is the whole analysis of the file: the parent
// indices in it refer to this vector, so scaffold_test_class needs all of it.
struct ParsedSource {
    std::string rel_path;  // under src/, '/' separated
    std::string package;   // empty for the default package
    std::vector<wb::Symbol> symbols;
};

// Parses one file with `session`, which is reused across calls.
ParsedSource parse_source(const std::string& rel_path, const std::string& text, wb::Session& session);

// A top-level declaration: its name, and where it is (index into the sources, index into that
// source's symbols).
struct TopLevelType {
    std::string name;
    std::size_t source = 0;
    std::size_t symbol = 0;
};

// Pure: class, interface, enum and record declarations with parent == -1, in source order.
std::vector<TopLevelType> top_level_types(const std::vector<ParsedSource>& sources);

// Pure: class names from test file names. "CartTest.java" and "demo/CartTest.java" both give
// "CartTest". Only names ending in "Test" are kept, and "Test" alone is dropped.
std::vector<std::string> test_class_names(const std::vector<std::string>& test_file_names);

// Pure: test_root/<package path>/<type_name>Test.java. The default package goes directly under
// test_root.
std::filesystem::path test_file_path(const std::filesystem::path& test_root,
                                     const std::string& package, const std::string& type_name);

// A stub file the action will create.
struct NewTest {
    std::filesystem::path path;
    std::string type_name;
    std::string contents;
};

// Decides which stub files to create. Missing classes come from wb::missing_test_classes, and
// a class whose file already exists (per `exists`) is left out. Two sources that declare the
// same type in the same package give one file.
std::vector<NewTest> plan_missing(const std::vector<ParsedSource>& sources,
                                  const std::vector<std::string>& test_names,
                                  const std::filesystem::path& test_root,
                                  const std::function<bool(const std::filesystem::path&)>& exists);

struct Report {
    std::vector<std::filesystem::path> created;  // absolute paths
    std::vector<std::string> errors;             // one line each
    std::size_t source_files = 0;
    std::size_t top_level_types = 0;
    std::size_t test_classes = 0;
    double elapsed_ms = 0.0;
    bool failed = false;  // no src/ folder, or nothing could be read
};

// Runs the action for the project folder: reads src/ and test/ (recursively), plans, and writes
// each new file with an exclusive create, so an existing file is never overwritten.
Report generate_missing_tests(const std::filesystem::path& project_dir);

// Plain text for the output panel: the created files, or "nothing missing", plus errors.
// Paths are shown relative to `project_dir`.
std::string format_report(const Report& report, const std::filesystem::path& project_dir);

}  // namespace app::test_gen
