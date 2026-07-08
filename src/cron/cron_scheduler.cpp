#include "cron/cron_scheduler.h"
#include "utils/logger.h"
#include "utils/path_utils.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>

namespace cbackup {

// Parse a single cron field into a sorted list of matching values
// field: e.g. "*" or "5" or "*/2" or "0,5,10"
// range: [min_val, max_val]
static std::vector<int> parse_field(const std::string& field,
                                     int min_val, int max_val) {
    std::vector<int> result;
    if (field == "*") {
        for (int i = min_val; i <= max_val; ++i) result.push_back(i);
        return result;
    }
    // Handle */step
    if (field.rfind("*/", 0) == 0) {
        int step = std::stoi(field.substr(2));
        if (step <= 0) throw std::invalid_argument("Invalid cron step: " + field);
        for (int i = min_val; i <= max_val; i += step) result.push_back(i);
        return result;
    }
    // Handle comma-separated values
    std::istringstream ss(field);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        // Handle range like "1-5"
        auto dash = tok.find('-');
        if (dash != std::string::npos) {
            int lo = std::stoi(tok.substr(0, dash));
            int hi = std::stoi(tok.substr(dash + 1));
            for (int i = lo; i <= hi; ++i) result.push_back(i);
        } else {
            result.push_back(std::stoi(tok));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

struct CronExpr {
    std::vector<int> minutes;  // 0-59
    std::vector<int> hours;    // 0-23
    std::vector<int> doms;     // 1-31
    std::vector<int> months;   // 1-12
    std::vector<int> dows;     // 0-6 (0=Sunday)
};

static CronExpr parse_cron(const std::string& expr) {
    std::istringstream ss(expr);
    std::vector<std::string> fields;
    std::string tok;
    while (ss >> tok) fields.push_back(tok);
    if (fields.size() != 5)
        throw std::invalid_argument("Cron expression must have 5 fields: "
                                    + expr);
    CronExpr ce;
    ce.minutes = parse_field(fields[0], 0, 59);
    ce.hours   = parse_field(fields[1], 0, 23);
    ce.doms    = parse_field(fields[2], 1, 31);
    ce.months  = parse_field(fields[3], 1, 12);
    ce.dows    = parse_field(fields[4], 0, 6);
    return ce;
}

static bool matches_cron(const CronExpr& ce, const struct tm& t) {
    auto in_vec = [](const std::vector<int>& v, int val) {
        return std::find(v.begin(), v.end(), val) != v.end();
    };
    return in_vec(ce.minutes, t.tm_min)
        && in_vec(ce.hours,   t.tm_hour)
        && in_vec(ce.doms,    t.tm_mday)
        && in_vec(ce.months,  t.tm_mon + 1)
        && in_vec(ce.dows,    t.tm_wday);
}

time_t next_trigger(const std::string& cron_expr, time_t from_time) {
    try {
        CronExpr ce = parse_cron(cron_expr);
        // Advance minute by minute from from_time+60
        time_t t = (from_time / 60 + 1) * 60;  // round up to next minute
        for (int i = 0; i < 366 * 24 * 60; ++i, t += 60) {
            struct tm* tm_info = localtime(&t);
            if (tm_info && matches_cron(ce, *tm_info))
                return t;
        }
    } catch (const std::exception& e) {
        Logger::error("Cron parse error: " + std::string(e.what()));
    }
    return -1;
}

void evict_old_backups(const std::string& dest_dir, int keep) {
    if (keep <= 0) return;

    // Collect all .cbk files sorted by mtime
    std::vector<std::pair<time_t, std::string>> entries;

    DIR* dirp = opendir(dest_dir.c_str());
    if (!dirp) return;

    struct dirent* ent;
    while ((ent = readdir(dirp)) != nullptr) {
        std::string name(ent->d_name);
        if (name.size() < 4) continue;
        if (name.substr(name.size() - 4) != ".cbk") continue;

        std::string full = path_utils::join(dest_dir, name);
        struct stat st;
        if (stat(full.c_str(), &st) == 0)
            entries.push_back({st.st_mtime, full});
    }
    closedir(dirp);

    // Sort oldest first
    std::sort(entries.begin(), entries.end());

    // Remove oldest until we have `keep` entries
    while (static_cast<int>(entries.size()) > keep) {
        Logger::info("Evicting old backup: " + entries.front().second);
        std::filesystem::remove(entries.front().second);
        entries.erase(entries.begin());
    }
}

int run_cron(const CronConfig& cfg,
             std::function<void(const CronConfig&)> on_backup) {
    Logger::info("Cron scheduler started. Schedule: " + cfg.schedule);

    try {
        // Validate expression early
        parse_cron(cfg.schedule);
    } catch (const std::exception& e) {
        Logger::error("Invalid cron expression: " + std::string(e.what()));
        return 1;
    }

    while (true) {
        time_t now = time(nullptr);
        time_t next = next_trigger(cfg.schedule, now);
        if (next < 0) {
            Logger::error("Could not compute next trigger time.");
            return 1;
        }

        int seconds_to_wait = static_cast<int>(next - now);
        if (seconds_to_wait > 0) {
            Logger::info("Next backup in " + std::to_string(seconds_to_wait)
                         + " seconds.");
            std::this_thread::sleep_for(std::chrono::seconds(seconds_to_wait));
        }

        Logger::info("Triggering scheduled backup.");
        try {
            on_backup(cfg);
            evict_old_backups(cfg.dest, cfg.keep);
        } catch (const std::exception& e) {
            Logger::error("Backup failed: " + std::string(e.what()));
        }
    }
    return 0;
}

}  // namespace cbackup
