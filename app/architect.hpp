#pragma once

// Project outline for the task graph's architect step: package, types and member counts for
// every .java file under <project>/src. Built with wb::Session (tree-sitter), no javac.

#include <cstddef>
#include <string>
#include <vector>

namespace app::architect {

// Caps: at most kMaxFiles files are read, and a file over kMaxFileBytes is skipped (recorded).
inline constexpr std::size_t kMaxFiles = 2000;
inline constexpr std::size_t kMaxFileBytes = 4u * 1024u * 1024u;

struct TypeEntry {
    std::string name;
    std::string kind;     // class, interface, enum, record
    bool nested = false;  // declared inside another type
    std::size_t methods = 0;  // direct methods (constructors not counted)
    std::size_t fields = 0;   // direct fields
};

struct FileEntry {
    std::string path;     // relative to the project's src directory, '/' separated
    std::string package;  // empty for the default package
    std::vector<TypeEntry> types;
};

struct Skipped {
    std::string path;
    std::string reason;
};

struct Outline {
    std::size_t files = 0;    // files parsed
    std::size_t types = 0;    // all type declarations, nested included
    std::size_t methods = 0;  // all methods, in every type
    std::size_t fields = 0;
    bool truncated = false;   // more than kMaxFiles files; the rest were not read
    std::vector<FileEntry> entries;  // one per parsed file
    std::vector<Skipped> skipped;
};

// One source text to outline. Used by outline_project and directly by tests.
struct Source {
    std::string path;
    std::string text;
};

// The first "package x.y;" line of a Java source, or empty for the default package.
std::string package_name(const std::string& text);

// Pure: parses each source and builds the outline. Sources past kMaxFiles are not read.
Outline outline_sources(const std::vector<Source>& sources);

// Reads <project_dir>/src recursively (sorted by path) and outlines it. Throws
// std::runtime_error when there is no src directory or no .java files.
Outline outline_project(const std::string& project_dir);

// One line: "<files> files, <types> types, <methods> methods" plus any skipped or truncated note.
std::string summarize(const Outline& outline);

}  // namespace app::architect
