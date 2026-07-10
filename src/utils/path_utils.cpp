#include "utils/path_utils.h"
#include <sys/stat.h>
#include <climits>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <cerrno>
#include <unistd.h>
#include <dirent.h>
#include <stack>

namespace cbackup {
namespace path_utils {

std::string normalize(const std::string& path) {
    if (path.empty()) return ".";
    // Use a stack-based approach to handle . and ..
    std::vector<std::string> parts;
    bool abs = (path[0] == '/');
    std::istringstream ss(path);
    std::string tok;
    while (std::getline(ss, tok, '/')) {
        if (tok.empty() || tok == ".") continue;
        if (tok == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(tok);
        }
    }
    std::string result = abs ? "/" : "";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += '/';
        result += parts[i];
    }
    return result.empty() ? "." : result;
}

bool exists(const std::string& path) {
    struct stat st;
    return lstat(path.c_str(), &st) == 0;
}

bool is_dir(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

std::string parent_dir(const std::string& path) {
    auto pos = path.rfind('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

std::string join(const std::string& base, const std::string& rel) {
    if (base.empty()) return rel;
    if (rel.empty()) return base;
    if (base.back() == '/') return base + rel;
    return base + "/" + rel;
}

std::string relative_to(const std::string& base, const std::string& full) {
    std::string b = base;
    if (!b.empty() && b.back() != '/') b += '/';
    if (full.substr(0, b.size()) == b)
        return full.substr(b.size());
    return full;
}

bool mkdir_p(const std::string& path) {
    if (path.empty()) return false;
    struct stat st;
    if (lstat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);

    // Create parent first
    std::string parent = parent_dir(path);
    if (parent != path && !parent.empty()) {
        if (!mkdir_p(parent)) return false;
    }
    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST)
        return false;
    return true;
}

bool validate_path(const std::string& path) {
    if (path.empty() || path.size() > PATH_MAX) return false;
    if (path.find('\0') != std::string::npos) return false;
    return true;
}

bool rm_dir_recursive(const std::string& path) {
    if (path.empty() || path == "/" || path == "." || path == "..")
        return false;  // safety: refuse to delete root / current dir

    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) {
        // Not a directory — just unlink
        return unlink(path.c_str()) == 0;
    }

    // Iterative DFS: collect all entries then delete bottom-up.
    using DirEntry = std::pair<std::string, bool>;  // (path, is_dir)
    std::vector<DirEntry> entries;
    std::stack<std::string> dirs;
    dirs.push(path);

    while (!dirs.empty()) {
        std::string dir = dirs.top();
        dirs.pop();
        entries.push_back({dir, true});

        DIR* dp = opendir(dir.c_str());
        if (!dp) continue;
        struct dirent* ent;
        while ((ent = readdir(dp)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            std::string full = join(dir, ent->d_name);
            struct stat child_st;
            if (lstat(full.c_str(), &child_st) != 0) continue;
            if (S_ISDIR(child_st.st_mode)) {
                dirs.push(full);
            } else {
                entries.push_back({full, false});
            }
        }
        closedir(dp);
    }

    // Delete in reverse order (children before parents).
    bool ok = true;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (it->second) {
            if (rmdir(it->first.c_str()) != 0) ok = false;
        } else {
            if (unlink(it->first.c_str()) != 0) ok = false;
        }
    }
    return ok;
}

}  // namespace path_utils
}  // namespace cbackup
