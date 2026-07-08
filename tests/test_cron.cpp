#include <gtest/gtest.h>
#include "cron/cron_scheduler.h"
#include "utils/path_utils.h"

#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace cbackup;
namespace fs = std::filesystem;

// Helper: create an empty file
static void touch_file(const std::string& path) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
}

TEST(CronScheduler, NextTrigger_Daily2AM) {
    struct tm t{};
    t.tm_year = 125;  // 2025
    t.tm_mon  = 0;    // January
    t.tm_mday = 1;
    t.tm_hour = 1;
    t.tm_min  = 0;
    t.tm_sec  = 0;
    time_t ref = mktime(&t);

    time_t next = next_trigger("0 2 * * *", ref);
    EXPECT_GT(next, ref);

    struct tm* result = localtime(&next);
    EXPECT_EQ(result->tm_hour, 2);
    EXPECT_EQ(result->tm_min,  0);
}

TEST(CronScheduler, NextTrigger_EveryMinute) {
    time_t now = time(nullptr);
    time_t next = next_trigger("* * * * *", now);
    EXPECT_GT(next, now);
    EXPECT_LE(next - now, 120);
}

TEST(CronScheduler, InvalidCronExpr) {
    time_t now = time(nullptr);
    time_t result = next_trigger("0 2 *", now);
    EXPECT_EQ(result, -1);
}

TEST(CronScheduler, EvictOldBackups) {
    std::string tmp = "/tmp/cbevict_" + std::to_string(getpid());
    path_utils::mkdir_p(tmp);

    touch_file(tmp + "/backup_001.cbk");
    touch_file(tmp + "/backup_002.cbk");
    touch_file(tmp + "/backup_003.cbk");
    touch_file(tmp + "/backup_004.cbk");
    touch_file(tmp + "/backup_005.cbk");

    evict_old_backups(tmp, 3);

    int count = 0;
    DIR* dirp = opendir(tmp.c_str());
    if (dirp) {
        struct dirent* ent;
        while ((ent = readdir(dirp)) != nullptr) {
            std::string name(ent->d_name);
            if (name.size() >= 4 && name.substr(name.size() - 4) == ".cbk")
                count++;
        }
        closedir(dirp);
    }
    EXPECT_EQ(count, 3);

    std::error_code ec;
    fs::remove_all(tmp, ec);
}
