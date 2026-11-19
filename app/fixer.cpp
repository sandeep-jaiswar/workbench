#include "fixer.hpp"

#include <cctype>
#include <filesystem>
#include <vector>

namespace app::fixer {
namespace fs = std::filesystem;

namespace {

bool is_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '.';
}

// Returns the qualified test name after the first "FAIL " token, or empty.
std::string first_failing_name(const std::string& output) {
    std::size_t at = 0;
    while ((at = output.find("FAIL ", at)) != std::string::npos) {
        const bool at_start = at == 0 || output[at - 1] == '\n' || output[at - 1] == ' ' ||
                              output[at - 1] == ';' || output[at - 1] == ',';
        const std::size_t begin = at + 5;
        std::size_t end = begin;
        while (end < output.size() && is_name_char(output[end])) ++end;
        if (at_start && end > begin) return output.substr(begin, end - begin);
        at = begin;
    }
    return {};
}

}  // namespace

std::string class_path_for_failure(const std::string& project_dir, const std::string& test_output) {
    const std::string name = first_failing_name(test_output);
    if (name.empty()) return {};

    // <pkg>.<Name>Test.<method>: split on '.', the last two parts are the class and method.
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '.') {
            parts.push_back(name.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() < 3) return {};  // needs a package, a class and a method
    if (parts.back().empty() || parts[parts.size() - 2].empty()) return {};

    const std::string& test_class = parts[parts.size() - 2];
    const std::string suffix = "Test";
    if (test_class.size() <= suffix.size() ||
        test_class.compare(test_class.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return {};
    }
    const std::string cls = test_class.substr(0, test_class.size() - suffix.size());

    fs::path rel = fs::path("src");
    for (std::size_t i = 0; i + 2 < parts.size(); ++i) rel /= parts[i];
    rel /= cls + ".java";

    std::error_code ec;
    if (!fs::is_regular_file(fs::path(project_dir) / rel, ec)) return {};
    return rel.generic_string();
}

}  // namespace app::fixer
