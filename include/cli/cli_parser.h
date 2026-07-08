#pragma once
#include <string>
#include <vector>

namespace cbackup {

enum class SubCommand {
    BACKUP,
    RESTORE,
    PACK,
    UNPACK,
    DAEMON,
    CRON,
    UNKNOWN,
};

struct CliArgs {
    SubCommand subcmd = SubCommand::UNKNOWN;

    // Common
    std::string source;
    std::string dest;
    bool verbose = false;

    // backup / pack
    bool compress = false;         // -z
    bool preserve_metadata = false; // -m
    bool special_files = true;     // enabled by default

    // filter (EX-03)
    std::vector<std::string> include_patterns;
    std::vector<std::string> exclude_patterns;

    // restore / unpack
    std::string archive_file;

    // daemon (EX-07)
    std::string watch_dir;
    std::string sync_dest;
    std::string log_file;

    // cron (EX-06)
    std::string cron_schedule;
    int keep = 5;
};

// Parse argc/argv. On error prints usage and returns false.
bool parse_args(int argc, char* argv[], CliArgs& out);

void print_usage(const char* prog);

}  // namespace cbackup
