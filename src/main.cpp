#include "cli/cli_parser.h"
#include "backup/backup.h"
#include "pack/packer.h"
#include "cron/cron_scheduler.h"
#include "daemon/inotify_daemon.h"
#include "compress/compressor.h"
#include "crypto/crypto.h"
#include "filter/filter.h"
#include "utils/logger.h"

#include <cstdlib>
#include <ctime>
#include <string>

using namespace cbackup;

// Assemble the 6-dimension FilterConfig from parsed CLI args.
// Returns false (and logs) on malformed --type/--mtime/--size/--owner specs.
static bool build_filter(const CliArgs& args, FilterConfig& fcfg) {
    fcfg.include_patterns = args.include_patterns;
    fcfg.exclude_patterns = args.exclude_patterns;

    if (!args.type_filter.empty()) {
        if (!parse_type_mask(args.type_filter, fcfg.type_mask)) {
            Logger::error("Invalid --type spec: " + args.type_filter);
            return false;
        }
    }
    if (!args.mtime_filter.empty()) {
        if (!parse_mtime_filter(args.mtime_filter, fcfg.mtime_newer,
                                fcfg.mtime_threshold)) {
            Logger::error("Invalid --mtime spec: " + args.mtime_filter);
            return false;
        }
        fcfg.has_mtime = true;
    }
    if (!args.size_filter.empty()) {
        if (!parse_size_filter(args.size_filter, fcfg.size_less,
                               fcfg.size_threshold)) {
            Logger::error("Invalid --size spec: " + args.size_filter);
            return false;
        }
        fcfg.has_size = true;
    }
    if (!args.owner_filter.empty()) {
        if (!resolve_username(args.owner_filter, fcfg.uid)) {
            Logger::error("Unknown user: " + args.owner_filter);
            return false;
        }
        fcfg.has_uid = true;
    }
    return true;
}

int main(int argc, char* argv[]) {
    CliArgs args;
    if (!parse_args(argc, argv, args)) return 1;

    Logger::set_verbose(args.verbose);

    // Build filter config from CLI (6 dimensions).
    FilterConfig fcfg;
    if (!build_filter(args, fcfg)) return 1;

    // Resolve compression / encryption algorithms.
    compress::Algorithm calgo = args.compress_algo.empty()
        ? (args.compress ? compress::Algorithm::ZLIB : compress::Algorithm::NONE)
        : compress::algo_from_string(args.compress_algo);
    crypto::Algorithm cipher = crypto::algo_from_string(args.encrypt_algo);

    if (cipher != crypto::Algorithm::NONE && args.password.empty()) {
        Logger::error("--encrypt requires a password (-p/--password).");
        return 1;
    }

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
            opts.compress_algo     = calgo;
            opts.cipher_algo       = cipher;
            opts.password          = args.password;
            opts.preserve_metadata = args.preserve_metadata;
            opts.special_files     = args.special_files;
            opts.filter            = &fcfg;
            opts.verbose           = args.verbose;
            auto stats = pack(opts);
            return stats.exit_code;
        }

        case SubCommand::UNPACK: {
            UnpackOptions opts;
            opts.archive  = args.archive_file;
            opts.dest     = args.dest;
            opts.password = args.password;
            opts.verbose  = args.verbose;
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
            cfg.compress_algo      = calgo;
            cfg.cipher_algo        = cipher;
            cfg.password           = args.password;
            cfg.filter             = fcfg;
            cfg.preserve_metadata  = args.preserve_metadata;
            cfg.verbose            = args.verbose;

            auto on_backup = [](const CronConfig& c) {
                PackOptions opts;
                time_t now = time(nullptr);
                char ts[32];
                strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", localtime(&now));
                opts.source            = c.source;
                opts.dest              = c.dest + "/backup_" + ts + ".cbk";
                opts.compress          = c.compress;
                opts.compress_algo     = c.compress_algo;
                opts.cipher_algo       = c.cipher_algo;
                opts.password          = c.password;
                opts.preserve_metadata = c.preserve_metadata;
                opts.special_files     = true;
                opts.filter            = &c.filter;
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
