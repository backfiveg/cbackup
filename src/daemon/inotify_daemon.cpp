#include "daemon/inotify_daemon.h"
#include "backup/backup.h"
#include "utils/fd_wrapper.h"
#include "utils/logger.h"
#include "utils/path_utils.h"

#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include <cerrno>
#include <cstring>
#include <ctime>
#include <map>
#include <stdexcept>
#include <string>

namespace cbackup {

static constexpr int INOTIFY_EVENTS =
    IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO;

static constexpr size_t EVENT_BUF_SIZE =
    1024 * (sizeof(struct inotify_event) + 16);

// Add watches recursively for all subdirectories
static void add_watches_recursive(int ifd,
                                   const std::string& path,
                                   std::map<int, std::string>& wd_to_path) {
    int wd = inotify_add_watch(ifd, path.c_str(), INOTIFY_EVENTS);
    if (wd < 0) {
        Logger::warn("inotify_add_watch failed: " + path);
        return;
    }
    wd_to_path[wd] = path;

    DIR* dirp = opendir(path.c_str());
    if (!dirp) return;

    struct dirent* ent;
    while ((ent = readdir(dirp)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        std::string child = path_utils::join(path, ent->d_name);
        struct stat st;
        if (lstat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            add_watches_recursive(ifd, child, wd_to_path);
    }
    closedir(dirp);
}

// Perform incremental backup of a single changed file/path
static void incremental_sync(const std::string& src_path,
                              const std::string& watch_root,
                              const std::string& sync_dest) {
    std::string rel = path_utils::relative_to(watch_root, src_path);
    std::string dst = path_utils::join(sync_dest, rel);

    struct stat st;
    if (lstat(src_path.c_str(), &st) != 0) {
        // File deleted: remove from dest
        if (errno == ENOENT) {
            Logger::info("Removing deleted: " + dst);
            std::remove(dst.c_str());
        }
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        path_utils::mkdir_p(dst);
        return;
    }

    // For regular files, copy with 64KB buffer
    path_utils::mkdir_p(path_utils::parent_dir(dst));

    FdWrapper src_fd(open(src_path.c_str(), O_RDONLY));
    if (!src_fd.valid()) { Logger::warn("open: " + src_path); return; }
    FdWrapper dst_fd(open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644));
    if (!dst_fd.valid()) { Logger::warn("create: " + dst); return; }

    char buf[64 * 1024];
    ssize_t n;
    while ((n = read(src_fd.get(), buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(dst_fd.get(), buf + written, n - written);
            if (w <= 0) break;
            written += w;
        }
    }
    Logger::info("Synced: " + rel);
}

static void daemon_loop(const DaemonConfig& cfg) {
    FdWrapper ifd(inotify_init());
    if (!ifd.valid()) {
        Logger::error("inotify_init failed: " + std::string(strerror(errno)));
        return;
    }

    std::map<int, std::string> wd_to_path;
    add_watches_recursive(ifd.get(), cfg.watch_dir, wd_to_path);
    Logger::info("Watching " + std::to_string(wd_to_path.size())
                 + " directories under " + cfg.watch_dir);

    std::vector<char> event_buf(EVENT_BUF_SIZE);
    while (true) {
        ssize_t len = read(ifd.get(), event_buf.data(), EVENT_BUF_SIZE);
        if (len <= 0) {
            if (errno == EINTR) continue;
            Logger::error("inotify read error: " + std::string(strerror(errno)));
            break;
        }

        ssize_t i = 0;
        while (i < len) {
            const auto* ev = reinterpret_cast<const struct inotify_event*>(
                event_buf.data() + i);

            if (ev->len > 0 && !(ev->mask & IN_ISDIR)) {
                auto it = wd_to_path.find(ev->wd);
                if (it != wd_to_path.end()) {
                    std::string full = path_utils::join(it->second, ev->name);
                    incremental_sync(full, cfg.watch_dir, cfg.sync_dest);
                }
            }

            // If a new directory is created, watch it too
            if (ev->len > 0 && (ev->mask & IN_CREATE) && (ev->mask & IN_ISDIR)) {
                auto it = wd_to_path.find(ev->wd);
                if (it != wd_to_path.end()) {
                    std::string new_dir = path_utils::join(it->second, ev->name);
                    add_watches_recursive(ifd.get(), new_dir, wd_to_path);
                }
            }

            i += sizeof(struct inotify_event) + ev->len;
        }
    }
}

int start_daemon(const DaemonConfig& cfg) {
    if (!path_utils::is_dir(cfg.watch_dir)) {
        Logger::error("Watch directory does not exist: " + cfg.watch_dir);
        return 1;
    }

    if (!path_utils::mkdir_p(cfg.sync_dest)) {
        Logger::error("Cannot create sync dest: " + cfg.sync_dest);
        return 1;
    }

    // Double-fork to daemonize
    pid_t pid = fork();
    if (pid < 0) {
        Logger::error("fork() failed");
        return 1;
    }
    if (pid > 0) {
        // Parent returns immediately
        printf("[INFO] cbackup daemon started (PID: %d)\n", pid);
        printf("[INFO] Watching: %s -> %s\n",
               cfg.watch_dir.c_str(), cfg.sync_dest.c_str());
        if (!cfg.log_file.empty())
            printf("[INFO] Log: %s\n", cfg.log_file.c_str());
        return 0;
    }

    // First child: create new session
    setsid();

    pid_t pid2 = fork();
    if (pid2 < 0) _exit(1);
    if (pid2 > 0) _exit(0);

    // Grandchild: the actual daemon
    // Redirect stdout/stderr to log file if specified
    if (!cfg.log_file.empty()) {
        int log_fd = open(cfg.log_file.c_str(),
                          O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }
    }

    // Close stdin
    int dev_null = open("/dev/null", O_RDONLY);
    if (dev_null >= 0) {
        dup2(dev_null, STDIN_FILENO);
        close(dev_null);
    }

    Logger::set_verbose(cfg.verbose);
    daemon_loop(cfg);
    return 0;
}

}  // namespace cbackup
