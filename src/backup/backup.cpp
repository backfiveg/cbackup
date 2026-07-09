#include "backup/backup.h"
#include "utils/logger.h"
#include "utils/path_utils.h"
#include "utils/fd_wrapper.h"
#include "filter/filter.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>

#include <cerrno>
#include <cstring>
#include <stack>
#include <stdexcept>
#include <string>

namespace cbackup {

static constexpr size_t IO_BUFFER_SIZE = 64 * 1024;  // 64KB

// Copy a regular file src -> dst, preserving metadata if requested
static bool copy_regular_file(const std::string& src,
                               const std::string& dst,
                               const struct stat& st,
                               bool preserve_metadata,
                               BackupStats& stats) {
    FdWrapper src_fd(open(src.c_str(), O_RDONLY));
    if (!src_fd.valid()) {
        Logger::warn("Cannot open " + src + ": " + strerror(errno));
        stats.files_skipped++;
        return false;
    }
    FdWrapper dst_fd(open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                          preserve_metadata ? (st.st_mode & 0777) : 0644));
    if (!dst_fd.valid()) {
        Logger::warn("Cannot create " + dst + ": " + strerror(errno));
        stats.files_skipped++;
        return false;
    }

    std::vector<uint8_t> buf(IO_BUFFER_SIZE);
    ssize_t n;
    while ((n = read(src_fd.get(), buf.data(), IO_BUFFER_SIZE)) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(dst_fd.get(), buf.data() + written, n - written);
            if (w <= 0) {
                Logger::error("Write failed for " + dst + ": " + strerror(errno));
                stats.files_skipped++;
                return false;
            }
            written += w;
        }
        stats.bytes_copied += static_cast<uint64_t>(n);
    }
    if (n < 0) {
        Logger::warn("Read error for " + src + ": " + strerror(errno));
    }

    if (preserve_metadata) {
        // Restore uid/gid (may fail if not root)
        if (fchown(dst_fd.get(), st.st_uid, st.st_gid) != 0) {
            Logger::warn("chown failed for " + dst);
        }
        fchmod(dst_fd.get(), st.st_mode & 07777);

        // Restore timestamps (nanosecond precision)
        struct timespec times[2];
        times[0] = st.st_atim;
        times[1] = st.st_mtim;
        if (utimensat(AT_FDCWD, dst.c_str(), times, 0) != 0) {
            Logger::warn("utimensat failed for " + dst);
        }
    }

    stats.files_ok++;
    return true;
}

// Handle symlink: readlink + symlink()
static bool backup_symlink(const std::string& src,
                            const std::string& dst,
                            const struct stat& st,
                            bool preserve_metadata,
                            BackupStats& stats) {
    char link_target[PATH_MAX + 1];
    ssize_t len = readlink(src.c_str(), link_target, PATH_MAX);
    if (len < 0) {
        Logger::warn("readlink failed: " + src);
        stats.files_skipped++;
        return false;
    }
    link_target[len] = '\0';

    if (symlink(link_target, dst.c_str()) != 0) {
        Logger::warn("symlink failed: " + dst);
        stats.files_skipped++;
        return false;
    }

    if (preserve_metadata) {
        lchown(dst.c_str(), st.st_uid, st.st_gid);
        struct timespec times[2];
        times[0] = st.st_atim;
        times[1] = st.st_mtim;
        utimensat(AT_FDCWD, dst.c_str(), times, AT_SYMLINK_NOFOLLOW);
    }

    stats.files_ok++;
    return true;
}

// Handle FIFO
static bool backup_fifo(const std::string& dst,
                         const struct stat& st,
                         bool preserve_metadata,
                         BackupStats& stats) {
    if (mkfifo(dst.c_str(), st.st_mode & 07777) != 0) {
        Logger::warn("mkfifo failed: " + dst);
        stats.files_skipped++;
        return false;
    }
    if (preserve_metadata) {
        chown(dst.c_str(), st.st_uid, st.st_gid);
    }
    stats.files_ok++;
    return true;
}

// Handle device files
static bool backup_device(const std::string& dst,
                           const struct stat& st,
                           bool preserve_metadata,
                           BackupStats& stats) {
    if (mknod(dst.c_str(), st.st_mode, st.st_rdev) != 0) {
        Logger::warn("mknod failed: " + dst + ": " + strerror(errno));
        stats.files_skipped++;
        return false;
    }
    if (preserve_metadata) {
        chown(dst.c_str(), st.st_uid, st.st_gid);
        chmod(dst.c_str(), st.st_mode & 07777);
    }
    stats.files_ok++;
    return true;
}

BackupStats backup(const BackupOptions& opts) {
    BackupStats stats;

    if (!path_utils::validate_path(opts.source) ||
        !path_utils::validate_path(opts.dest)) {
        Logger::error("Invalid path specified.");
        stats.exit_code = 1;
        return stats;
    }

    if (!path_utils::exists(opts.source)) {
        Logger::error("Source does not exist: " + opts.source);
        stats.exit_code = 1;
        return stats;
    }

    if (!path_utils::is_dir(opts.source)) {
        Logger::error("Source is not a directory: " + opts.source);
        stats.exit_code = 1;
        return stats;
    }

    if (!path_utils::mkdir_p(opts.dest)) {
        Logger::error("Cannot create destination: " + opts.dest);
        stats.exit_code = 1;
        return stats;
    }

    std::string norm_src = path_utils::normalize(opts.source);
    std::string norm_dst = path_utils::normalize(opts.dest);

    Filter filter(opts.filter);

    // Iterative DFS using stack<pair<src_path, dst_path>>
    using DirPair = std::pair<std::string, std::string>;
    std::stack<DirPair> dir_stack;
    dir_stack.push({norm_src, norm_dst});

    while (!dir_stack.empty()) {
        auto [src_dir, dst_dir] = dir_stack.top();
        dir_stack.pop();

        DIR* dirp = opendir(src_dir.c_str());
        if (!dirp) {
            Logger::warn("Cannot open dir: " + src_dir + ": " + strerror(errno));
            stats.files_skipped++;
            continue;
        }

        struct dirent* ent;
        while ((ent = readdir(dirp)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;

            std::string src_path = path_utils::join(src_dir, ent->d_name);
            std::string rel_path = path_utils::relative_to(norm_src, src_path);
            std::string dst_path = path_utils::join(dst_dir, ent->d_name);

            // Apply filter (only for non-directories)
            struct stat st;
            if (lstat(src_path.c_str(), &st) != 0) {
                Logger::warn("lstat failed: " + src_path);
                stats.files_skipped++;
                continue;
            }

            if (!S_ISDIR(st.st_mode) && !filter.should_include(rel_path, st)) {
                Logger::info("Filtered out: " + rel_path);
                stats.files_skipped++;
                continue;
            }

            try {
                if (S_ISDIR(st.st_mode)) {
                    path_utils::mkdir_p(dst_path);
                    if (opts.preserve_metadata) {
                        chown(dst_path.c_str(), st.st_uid, st.st_gid);
                        chmod(dst_path.c_str(), st.st_mode & 07777);
                    }
                    dir_stack.push({src_path, dst_path});

                } else if (S_ISLNK(st.st_mode) && opts.special_files) {
                    Logger::info("Symlink: " + rel_path);
                    backup_symlink(src_path, dst_path, st,
                                   opts.preserve_metadata, stats);

                } else if (S_ISFIFO(st.st_mode) && opts.special_files) {
                    Logger::info("FIFO: " + rel_path);
                    backup_fifo(dst_path, st, opts.preserve_metadata, stats);

                } else if ((S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
                           && opts.special_files) {
                    Logger::info("Device: " + rel_path);
                    backup_device(dst_path, st, opts.preserve_metadata, stats);

                } else if (S_ISREG(st.st_mode)) {
                    Logger::info("File: " + rel_path);
                    copy_regular_file(src_path, dst_path, st,
                                      opts.preserve_metadata, stats);
                } else {
                    Logger::warn("Skipping unknown type: " + rel_path);
                    stats.files_skipped++;
                }
            } catch (const std::exception& e) {
                Logger::error(std::string("Exception: ") + e.what()
                              + " at " + rel_path);
                stats.files_skipped++;
            }

            if (stats.files_ok % 100 == 0)
                Logger::progress(stats.files_ok, stats.bytes_copied);
        }
        closedir(dirp);
    }

    printf("\n[DONE] Backup complete: %lu files, %.2f MB\n",
           stats.files_ok,
           static_cast<double>(stats.bytes_copied) / (1024.0 * 1024.0));
    return stats;
}

}  // namespace cbackup
