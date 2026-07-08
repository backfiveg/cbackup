#pragma once
#include <string>
#include <vector>

namespace cbackup {
namespace path_utils {

// Normalize path: resolve . and .. without following symlinks
std::string normalize(const std::string& path);

// Check path exists (using lstat, no symlink follow)
bool exists(const std::string& path);

// Check if directory
bool is_dir(const std::string& path);

// Get parent directory
std::string parent_dir(const std::string& path);

// Join two paths
std::string join(const std::string& base, const std::string& rel);

// Get relative path of child from base
std::string relative_to(const std::string& base, const std::string& full);

// Create directories recursively (like mkdir -p)
bool mkdir_p(const std::string& path);

// Validate path: no null bytes, reasonable length
bool validate_path(const std::string& path);

}  // namespace path_utils
}  // namespace cbackup
