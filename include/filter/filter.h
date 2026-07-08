#pragma once
#include <string>
#include <vector>
#include <regex>

namespace cbackup {

struct FilterConfig {
    std::vector<std::string> include_patterns;  // e.g. "*.cpp"
    std::vector<std::string> exclude_patterns;  // e.g. "*.tmp", "build/"
};

class Filter {
 public:
    explicit Filter(const FilterConfig& cfg);

    // Returns true if the given filename/path should be included in backup
    bool should_include(const std::string& rel_path) const;

 private:
    // Convert glob pattern to regex
    static std::regex glob_to_regex(const std::string& pattern);

    std::vector<std::regex> includes_;
    std::vector<std::regex> excludes_;
    bool has_includes_;
};

}  // namespace cbackup
