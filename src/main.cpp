#include "cli/cli_parser.h"
#include "backup/backup.h"
#include "pack/packer.h"
#include "cron/cron_scheduler.h"
#include "daemon/inotify_daemon.h"
#include "utils/logger.h"

#include <cstdlib>

using namespace cbackup;

int main(int argc, char* argv[]) {
    CliArgs args;
    if (!parse_args(argc, argv, args)) return 1;

    Logger::set_verbose(args.verbose);

    // Build filter config from CLI
    FilterConfig fcfg;
    fcfg.include_patterns = args.include_patterns;
    fcfg.exclude_patterns = args.exclude_patterns;

    switch (args.subcmd) {
        case SubCommand::BACKUP: {
            BackupOptions opts;
            opts.source            = args.source;
            opts.dest              = args.dest;
            opts.preserve_metadata = args.preserve_metadata;
            opts.special_files     = args.special_files;
            opts.filter            = fcfg;
            opts.verbose           = args.verbose;
            auto stats = backup(opts);
            return stats.exit_code;
        }

        case SubCommand::RESTORE: {
            RestoreOptions opts;
            opts.source  = args.source;
            opts.dest    = args.dest;
            opts.verbose = args.verbose;
            auto stats = restore_dir(opts);
            return stats.exit_code;
        }

        case SubCommand::PACK: {
            PackOptions opts;
            opts.source            = args.source;
            opts.dest              = args.dest;
            opts.compress          = args.compress;
            opts.preserve_metadata = args.preserve_metadata;
            opts.special_files     = args.special_files;
            opts.filter            = &fcfg;
            opts.verbose           = args.verbose;
            auto stats = pack(opts);
            return stats.exit_code;
        }

        case SubCommand::UNPACK: {
            UnpackOptions opts;
            opts.archive = args.archive_file;
            opts.dest    = args.dest;
            opts.verbose = args.verbose;
            auto stats = unpack(opts);
            return stats.exit_code;
        }

        case SubCommand::DAEMON: {
            DaemonConfig cfg;
            cfg.watch_dir  = args.watch_dir;
            cfg.sync_dest  = args.sync_dest;
            cfg.log_file   = args.log_file;
            cfg.verbose    = args.verbose;
            return start_daemon(cfg);
        }

        case SubCommand::CRON: {
            CronConfig cfg;
            cfg.schedule           = args.cron_schedule;
            cfg.source             = args.source;
            cfg.dest               = args.dest;
            cfg.keep               = args.keep;
            cfg.compress           = args.compress;
            cfg.preserve_metadata  = args.preserve_metadata;
            cfg.verbose            = args.verbose;

            auto on_backup = [](const CronConfig& c) {
                PackOptions opts;
                // Use timestamp in filename
                time_t now = time(nullptr);
                char ts[32];
                strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", localtime(&now));
                opts.source            = c.source;
                opts.dest              = c.dest + "/backup_" + ts + ".cbk";
                opts.compress          = c.compress;
                opts.preserve_metadata = c.preserve_metadata;
                opts.special_files     = true;
                opts.verbose           = c.verbose;
                pack(opts);
            };

            return run_cron(cfg, on_backup);
        }

        default:
            print_usage(argv[0]);
            return 1;
    }
}
