#pragma once
#include <string>

namespace cbackup {

struct DaemonConfig {
    std::string watch_dir;
    std::string sync_dest;
    std::string log_file;
    bool verbose = false;
};

// Start the inotify daemon (daemonizes the process)
// Returns 0 on success in the parent process; daemon runs in background
int start_daemon(const DaemonConfig& cfg);

}  // namespace cbackup
