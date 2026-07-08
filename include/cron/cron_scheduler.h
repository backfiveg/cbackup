#pragma once
#include <string>
#include <functional>

namespace cbackup {

struct CronConfig {
    std::string schedule;    // e.g. "0 2 * * *"
    std::string source;
    std::string dest;
    int keep = 0;            // 0 = keep all; >0 = keep N latest
    bool compress = false;
    bool preserve_metadata = false;
    bool verbose = false;
};

// Parse a cron expression "min hour dom mon dow"
// Returns next trigger time_t relative to now; -1 on parse error
time_t next_trigger(const std::string& cron_expr, time_t from_time);

// Run the cron scheduler (blocks the calling thread)
// on_backup: called each time a backup should be triggered
int run_cron(const CronConfig& cfg,
             std::function<void(const CronConfig&)> on_backup);

// Evict old backups keeping only `keep` most recent entries in dest dir
void evict_old_backups(const std::string& dest_dir, int keep);

}  // namespace cbackup
