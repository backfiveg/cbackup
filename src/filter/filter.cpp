#include "filter/filter.h"

#include <pwd.h>

#include <cctype>
#include <cstdint>
#include <ctime>
#include <stdexcept>
#include <string>

namespace cbackup {

std::regex Filter::glob_to_regex(const std::string& pattern) {
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

Filter::Filter(const FilterConfig& cfg)
    : has_includes_(!cfg.include_patterns.empty()), cfg_(cfg) {
    for (const auto& p : cfg.include_patterns)
        includes_.push_back(glob_to_regex(p));
    for (const auto& p : cfg.exclude_patterns)
        excludes_.push_back(glob_to_regex(p));
}

bool Filter::name_pass(const std::string& rel_path) const {
    // Extract just the filename for matching.
    std::string filename = rel_path;
    auto slash = rel_path.rfind('/');
    if (slash != std::string::npos) filename = rel_path.substr(slash + 1);

    // Exclude wins (matched against both filename and full relative path).
    for (const auto& re : excludes_) {
        if (std::regex_match(filename, re) || std::regex_match(rel_path, re))
            return false;
    }

    if (has_includes_) {
        for (const auto& re : includes_) {
            if (std::regex_match(filename, re) || std::regex_match(rel_path, re))
                return true;
        }
        return false;
    }
    return true;
}

bool Filter::should_include(const std::string& rel_path) const {
    return name_pass(rel_path);
}

bool Filter::should_include(const std::string& rel_path,
                            const struct stat& st) const {
    // (1)(2) name & path
    if (!name_pass(rel_path)) return false;

    // (3) type
    if (cfg_.type_mask != 0) {
        uint32_t bit = 0;
        if (S_ISREG(st.st_mode))       bit = TYPE_REGULAR;
        else if (S_ISLNK(st.st_mode))  bit = TYPE_SYMLINK;
        else if (S_ISDIR(st.st_mode))  bit = TYPE_DIR;
        else                           bit = TYPE_SPECIAL;
        if (!(cfg_.type_mask & bit)) return false;
    }

    // (4) time (mtime)
    if (cfg_.has_mtime) {
        if (cfg_.mtime_newer) {
            if (st.st_mtime < cfg_.mtime_threshold) return false;
        } else {
            if (st.st_mtime > cfg_.mtime_threshold) return false;
        }
    }

    // (5) size — only meaningful for regular files
    if (cfg_.has_size && S_ISREG(st.st_mode)) {
        uint64_t sz = static_cast<uint64_t>(st.st_size);
        if (cfg_.size_less) {
            if (!(sz < cfg_.size_threshold)) return false;
        } else {
            if (!(sz > cfg_.size_threshold)) return false;
        }
    }

    // (6) user (uid)
    if (cfg_.has_uid) {
        if (st.st_uid != cfg_.uid) return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// CLI spec parsers
// ---------------------------------------------------------------------------

bool parse_type_mask(const std::string& spec, uint32_t& mask) {
    mask = 0;
    std::string token;
    auto flush = [&]() -> bool {
        if (token.empty()) return true;
        for (auto& c : token) c = static_cast<char>(std::tolower(c));
        if (token == "regular" || token == "reg" || token == "file")
            mask |= TYPE_REGULAR;
        else if (token == "symlink" || token == "link" || token == "lnk")
            mask |= TYPE_SYMLINK;
        else if (token == "dir" || token == "directory")
            mask |= TYPE_DIR;
        else if (token == "special" || token == "dev" || token == "fifo")
            mask |= TYPE_SPECIAL;
        else
            return false;
        token.clear();
        return true;
    };
    for (char c : spec) {
        if (c == ',' || c == '|') {
            if (!flush()) return false;
        } else {
            token.push_back(c);
        }
    }
    return flush() && mask != 0;
}

bool parse_size_filter(const std::string& spec, bool& less, uint64_t& bytes) {
    if (spec.size() < 2) return false;
    size_t i = 0;
    if (spec[0] == '<') { less = true; i = 1; }
    else if (spec[0] == '>') { less = false; i = 1; }
    else return false;

    // Parse the numeric part.
    uint64_t num = 0;
    bool any = false;
    while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i]))) {
        num = num * 10 + static_cast<uint64_t>(spec[i] - '0');
        any = true;
        ++i;
    }
    if (!any) return false;

    // Optional unit suffix.
    uint64_t mult = 1;
    if (i < spec.size()) {
        char u = static_cast<char>(std::toupper(spec[i]));
        switch (u) {
            case 'B': mult = 1ULL; break;
            case 'K': mult = 1024ULL; break;
            case 'M': mult = 1024ULL * 1024; break;
            case 'G': mult = 1024ULL * 1024 * 1024; break;
            default: return false;
        }
        ++i;
        // Allow a trailing 'B' (e.g. "500MB").
        if (i < spec.size() && std::toupper(spec[i]) == 'B') ++i;
        if (i != spec.size()) return false;
    }
    bytes = num * mult;
    return true;
}

bool parse_mtime_filter(const std::string& spec, bool& newer, time_t& epoch) {
    if (spec.size() < 2) return false;
    size_t i = 0;
    if (spec[0] == '-') { newer = true; i = 1; }   // within last N days
    else if (spec[0] == '+') { newer = false; i = 1; }  // older than N days
    else return false;

    long days = 0;
    bool any = false;
    while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i]))) {
        days = days * 10 + (spec[i] - '0');
        any = true;
        ++i;
    }
    if (!any) return false;
    // Optional 'd' suffix.
    if (i < spec.size()) {
        if (std::tolower(spec[i]) == 'd') ++i;
        if (i != spec.size()) return false;
    }
    epoch = time(nullptr) - days * 24 * 3600;
    return true;
}

bool resolve_username(const std::string& name, uid_t& uid) {
    if (name.empty()) return false;
    // Numeric uid?
    bool numeric = true;
    for (char c : name)
        if (!std::isdigit(static_cast<unsigned char>(c))) { numeric = false; break; }
    if (numeric) {
        uid = static_cast<uid_t>(std::stoul(name));
        return true;
    }
    struct passwd* pw = getpwnam(name.c_str());
    if (!pw) return false;
    uid = pw->pw_uid;
    return true;
}

}  // namespace cbackup
