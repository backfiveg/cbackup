#include "filter/filter.h"
#include <stdexcept>

namespace cbackup {

static std::regex glob_to_regex(const std::string& pattern) {
    std::string re = "^";
    for (char c : pattern) {
        switch (c) {
            case '*': re += ".*"; break;
            case '?': re += ".";  break;
            case '.': re += "\\."; break;
            case '/': re += "/";  break;
            case '[': re += "\\["; break;
            case ']': re += "\\]"; break;
            case '(': re += "\\("; break;
            case ')': re += "\\)"; break;
            case '{': re += "\\{"; break;
            case '}': re += "\\}"; break;
            case '^': re += "\\^"; break;
            case '$': re += "\\$"; break;
            case '+': re += "\\+"; break;
            default:  re += c;    break;
        }
    }
    re += "$";
    return std::regex(re, std::regex::ECMAScript | std::regex::icase);
}

Filter::Filter(const FilterConfig& cfg) : has_includes_(!cfg.include_patterns.empty()) {
    for (const auto& p : cfg.include_patterns)
        includes_.push_back(glob_to_regex(p));
    for (const auto& p : cfg.exclude_patterns)
        excludes_.push_back(glob_to_regex(p));
}

bool Filter::should_include(const std::string& rel_path) const {
    // Extract just the filename for matching
    std::string filename = rel_path;
    auto slash = rel_path.rfind('/');
    if (slash != std::string::npos) filename = rel_path.substr(slash + 1);

    // Check excludes first (both against filename and full rel path)
    for (const auto& re : excludes_) {
        if (std::regex_match(filename, re) || std::regex_match(rel_path, re))
            return false;
    }

    // If includes specified, must match at least one
    if (has_includes_) {
        for (const auto& re : includes_) {
            if (std::regex_match(filename, re) || std::regex_match(rel_path, re))
                return true;
        }
        return false;
    }

    return true;
}

}  // namespace cbackup
