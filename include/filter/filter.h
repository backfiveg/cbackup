#pragma once
#include <sys/stat.h>
#include <sys/types.h>

#include <cstdint>
#include <regex>
#include <string>
#include <vector>

namespace cbackup {

// Type bitmask for the "by type" filter dimension.
enum TypeBit : uint32_t {
    TYPE_REGULAR = 1u << 0,
    TYPE_SYMLINK = 1u << 1,
    TYPE_DIR     = 1u << 2,
    TYPE_SPECIAL = 1u << 3,  // fifo / char dev / block dev
};

// Full 6-dimension custom-backup configuration (EX-03).
struct FilterConfig {
    // (1) by name & (2) by path — glob patterns matched against filename/relpath
    std::vector<std::string> include_patterns;  // e.g. "*.cpp"
    std::vector<std::string> exclude_patterns;  // e.g. "*.tmp", ".git/*"

    // (3) by type — 0 means "no type restriction" (all types pass)
    uint32_t type_mask = 0;

    // (4) by time — filter on st_mtime
    bool     has_mtime = false;
    bool     mtime_newer = true;   // true: keep files newer-or-equal than threshold
    time_t   mtime_threshold = 0;  // absolute epoch seconds

    // (5) by size — filter on st_size (regular files only)
    bool     has_size = false;
    bool     size_less = true;     // true: keep size < threshold; false: size > threshold
    uint64_t size_threshold = 0;

    // (6) by user — filter on st_uid
    bool     has_uid = false;
    uid_t    uid = 0;
};

class Filter {
 public:
    explicit Filter(const FilterConfig& cfg);

    // Name/path-only decision (dimensions 1 & 2). Kept for callers/tests that
    // do not have a struct stat at hand.
    bool should_include(const std::string& rel_path) const;

    // Full 6-dimension decision using file metadata.
    bool should_include(const std::string& rel_path, const struct stat& st) const;

 private:
    static std::regex glob_to_regex(const std::string& pattern);
    bool name_pass(const std::string& rel_path) const;

    std::vector<std::regex> includes_;
    std::vector<std::regex> excludes_;
    bool has_includes_;
    FilterConfig cfg_;
};

// --- CLI spec parsers for the extended filter dimensions ---
// "regular,symlink,dir,special" -> bitmask. Returns false on unknown token.
bool parse_type_mask(const std::string& spec, uint32_t& mask);
// "<500M" / ">100K" / "<1G" / ">1024" -> (less, threshold_bytes).
bool parse_size_filter(const std::string& spec, bool& less, uint64_t& bytes);
// "-7d" (within 7 days) / "+30d" (older than 30 days) / "-7" -> (newer, epoch).
bool parse_mtime_filter(const std::string& spec, bool& newer, time_t& epoch);
// Resolve a username to uid via getpwnam(); accepts a numeric uid too.
bool resolve_username(const std::string& name, uid_t& uid);

}  // namespace cbackup
