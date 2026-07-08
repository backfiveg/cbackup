#include "backup/backup.h"
#include "utils/logger.h"
#include "utils/path_utils.h"
#include "utils/fd_wrapper.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stack>
#include <string>
#include <vector>

namespace cbackup {

static constexpr size_t IO_BUFFER_SIZE = 64 * 1024;

RestoreStats restore_dir(const RestoreOptions& opts) {
    RestoreStats stats;

    if (!path_utils::validate_path(opts.source) ||
        !path_utils::validate_path(opts.dest)) {
        Logger::error("Invalid path.");
        stats.exit_code = 1;
        return stats;
    }

    if (!path_utils::is_dir(opts.source)) {
        Logger::error("Backup source is not a directory: " + opts.source);
        stats.exit_code = 1;
        return stats;
    }

    if (!path_utils::mkdir_p(opts.dest)) {
        Logger::error("Cannot create dest: " + opts.dest);
        stats.exit_code = 1;
        return stats;
    }

    std::string norm_src = path_utils::normalize(opts.source);
    std::string norm_dst = path_utils::normalize(opts.dest);

    using DirPair = std::pair<std::string, std::string>;
    std::stack<DirPair> dir_stack;
    dir_stack.push({norm_src, norm_dst});

    while (!dir_stack.empty()) {
        auto [src_dir, dst_dir] = dir_stack.top();
        dir_stack.pop();

        DIR* dirp = opendir(src_dir.c_str());
        if (!dirp) {
            Logger::warn("Cannot open: " + src_dir);
            continue;
        }

        struct dirent* ent;
        while ((ent = readdir(dirp)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;

            std::string src_path = path_utils::join(src_dir, ent->d_name);
            std::string dst_path = path_utils::join(dst_dir, ent->d_name);

            struct stat st;
            if (lstat(src_path.c_str(), &st) != 0) {
                Logger::warn("lstat failed: " + src_path);
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                path_utils::mkdir_p(dst_path);
                dir_stack.push({src_path, dst_path});
                continue;
            }

            // Regular file copy
            if (S_ISREG(st.st_mode)) {
                FdWrapper in(open(src_path.c_str(), O_RDONLY));
                if (!in.valid()) {
                    Logger::warn("open failed: " + src_path);
                    continue;
                }
                FdWrapper out(open(dst_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644));
                if (!out.valid()) {
                    Logger::warn("create failed: " + dst_path);
                    continue;
                }
                std::vector<uint8_t> buf(IO_BUFFER_SIZE);
                ssize_t n;
                while ((n = read(in.get(), buf.data(), IO_BUFFER_SIZE)) > 0) {
                    ssize_t written = 0;
                    while (written < n) {
                        ssize_t w = write(out.get(), buf.data() + written, n - written);
                        if (w <= 0) break;
                        written += w;
                    }
                    stats.bytes_written += static_cast<uint64_t>(n);
                }
                stats.files_restored++;
                Logger::info("Restored: " + dst_path);

            } else if (S_ISLNK(st.st_mode)) {
                char target[PATH_MAX + 1];
                ssize_t len = readlink(src_path.c_str(), target, PATH_MAX);
                if (len > 0) {
                    target[len] = '\0';
                    symlink(target, dst_path.c_str());
                    stats.files_restored++;
                }
            } else if (S_ISFIFO(st.st_mode)) {
                mkfifo(dst_path.c_str(), st.st_mode & 07777);
                stats.files_restored++;
            } else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
                mknod(dst_path.c_str(), st.st_mode, st.st_rdev);
                stats.files_restored++;
            }
        }
        closedir(dirp);
    }

    printf("[DONE] Restore complete: %lu files, %.2f MB\n",
           stats.files_restored,
           static_cast<double>(stats.bytes_written) / (1024.0 * 1024.0));
    return stats;
}

}  // namespace cbackup
