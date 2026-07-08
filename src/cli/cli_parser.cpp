#include "cli/cli_parser.h"
#include <cstring>
#include <cstdio>
#include <string>

namespace cbackup {

void print_usage(const char* prog) {
    printf(
        "Usage: %s <subcommand> [options]\n"
        "\n"
        "Subcommands:\n"
        "  backup    Copy-based directory backup\n"
        "  restore   Restore from a backup directory\n"
        "  pack      Pack a directory into a .cbk archive\n"
        "  unpack    Unpack a .cbk archive\n"
        "  daemon    Start inotify real-time incremental sync daemon\n"
        "  cron      Run scheduled backup with eviction policy\n"
        "\n"
        "Common options:\n"
        "  --source <path>    Source directory\n"
        "  --dest   <path>    Destination directory\n"
        "  --verbose          Enable verbose logging\n"
        "\n"
        "backup options:\n"
        "  -m, --metadata     Preserve file metadata (uid/gid/mode/timestamps)\n"
        "  --include <glob>   Include pattern (e.g. '*.cpp')\n"
        "  --exclude <glob>   Exclude pattern (e.g. '*.tmp')\n"
        "\n"
        "pack options:\n"
        "  -z, --compress     Compress archive with zlib\n"
        "  -m, --metadata     Preserve metadata\n"
        "  --include <glob>   Include pattern\n"
        "  --exclude <glob>   Exclude pattern\n"
        "\n"
        "unpack options:\n"
        "  --file <path>      Archive file (.cbk)\n"
        "  --dest <path>      Destination directory\n"
        "\n"
        "restore options:\n"
        "  --source <path>    Backup directory\n"
        "  --dest <path>      Restore destination\n"
        "\n"
        "daemon options:\n"
        "  --watch <path>     Directory to watch\n"
        "  --sync-to <path>   Sync destination\n"
        "  --log <path>       Log file path\n"
        "\n"
        "cron options:\n"
        "  --schedule <expr>  Cron expression (e.g. '0 2 * * *')\n"
        "  --source <path>    Source directory\n"
        "  --dest <path>      Destination directory\n"
        "  --keep <N>         Keep N latest backups (0 = unlimited)\n"
        "  -z, --compress     Compress backups\n"
        "  -m, --metadata     Preserve metadata\n"
        "\n"
        "Examples:\n"
        "  %s backup --source /home/user/docs --dest /backup/docs\n"
        "  %s pack -z -m --source /home/user/docs --dest /backup/docs.cbk\n"
        "  %s unpack --file /backup/docs.cbk --dest /restore/docs\n"
        "  %s restore --source /backup/docs --dest /restore/docs\n"
        "  %s daemon --watch /home/user/docs --sync-to /backup/rt --log /var/log/cb.log\n"
        "  %s cron --schedule \"0 2 * * *\" --source /home/user/docs --dest /backup --keep 5\n",
        prog, prog, prog, prog, prog, prog, prog);
}

bool parse_args(int argc, char* argv[], CliArgs& out) {
    if (argc < 2) {
        print_usage(argv[0]);
        return false;
    }

    // Determine subcommand
    const char* sub = argv[1];
    if (strcmp(sub, "backup") == 0) {
        out.subcmd = SubCommand::BACKUP;
    } else if (strcmp(sub, "restore") == 0) {
        out.subcmd = SubCommand::RESTORE;
    } else if (strcmp(sub, "pack") == 0) {
        out.subcmd = SubCommand::PACK;
    } else if (strcmp(sub, "unpack") == 0) {
        out.subcmd = SubCommand::UNPACK;
    } else if (strcmp(sub, "daemon") == 0) {
        out.subcmd = SubCommand::DAEMON;
    } else if (strcmp(sub, "cron") == 0) {
        out.subcmd = SubCommand::CRON;
    } else {
        fprintf(stderr, "Unknown subcommand: %s\n", sub);
        print_usage(argv[0]);
        return false;
    }

    for (int i = 2; i < argc; ++i) {
        const char* arg = argv[i];
        auto next_arg = [&]() -> const char* {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option %s requires a value.\n", arg);
                return nullptr;
            }
            return argv[++i];
        };

        if (strcmp(arg, "--source") == 0) {
            const char* v = next_arg(); if (!v) return false;
            out.source = v;
        } else if (strcmp(arg, "--dest") == 0) {
            const char* v = next_arg(); if (!v) return false;
            out.dest = v;
        } else if (strcmp(arg, "--file") == 0) {
            const char* v = next_arg(); if (!v) return false;
            out.archive_file = v;
        } else if (strcmp(arg, "--watch") == 0) {
            const char* v = next_arg(); if (!v) return false;
            out.watch_dir = v;
        } else if (strcmp(arg, "--sync-to") == 0) {
            const char* v = next_arg(); if (!v) return false;
            out.sync_dest = v;
        } else if (strcmp(arg, "--log") == 0) {
            const char* v = next_arg(); if (!v) return false;
            out.log_file = v;
        } else if (strcmp(arg, "--schedule") == 0) {
            const char* v = next_arg(); if (!v) return false;
            out.cron_schedule = v;
        } else if (strcmp(arg, "--keep") == 0) {
            const char* v = next_arg(); if (!v) return false;
            out.keep = std::stoi(v);
        } else if (strcmp(arg, "--include") == 0) {
            const char* v = next_arg(); if (!v) return false;
            out.include_patterns.push_back(v);
        } else if (strcmp(arg, "--exclude") == 0) {
            const char* v = next_arg(); if (!v) return false;
            out.exclude_patterns.push_back(v);
        } else if (strcmp(arg, "-z") == 0 || strcmp(arg, "--compress") == 0) {
            out.compress = true;
        } else if (strcmp(arg, "-m") == 0 || strcmp(arg, "--metadata") == 0) {
            out.preserve_metadata = true;
        } else if (strcmp(arg, "--verbose") == 0 || strcmp(arg, "-v") == 0) {
            out.verbose = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            print_usage(argv[0]);
            return false;
        }
    }

    // Validate required args
    switch (out.subcmd) {
        case SubCommand::BACKUP:
        case SubCommand::RESTORE:
            if (out.source.empty() || out.dest.empty()) {
                fprintf(stderr, "backup/restore requires --source and --dest\n");
                return false;
            }
            break;
        case SubCommand::PACK:
            if (out.source.empty() || out.dest.empty()) {
                fprintf(stderr, "pack requires --source and --dest\n");
                return false;
            }
            break;
        case SubCommand::UNPACK:
            if (out.archive_file.empty() || out.dest.empty()) {
                fprintf(stderr, "unpack requires --file and --dest\n");
                return false;
            }
            break;
        case SubCommand::DAEMON:
            if (out.watch_dir.empty() || out.sync_dest.empty()) {
                fprintf(stderr, "daemon requires --watch and --sync-to\n");
                return false;
            }
            break;
        case SubCommand::CRON:
            if (out.cron_schedule.empty() || out.source.empty()
                || out.dest.empty()) {
                fprintf(stderr, "cron requires --schedule, --source, --dest\n");
                return false;
            }
            break;
        default:
            break;
    }

    return true;
}

}  // namespace cbackup
