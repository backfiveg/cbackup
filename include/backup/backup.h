#pragma once
#include <string>
#include <vector>
#include "filter/filter.h"

namespace cbackup {

struct BackupOptions {
    std::string source;
    std::string dest;
    bool preserve_metadata = false;   // EX-02
    bool special_files = false;       // EX-01
    FilterConfig filter;              // EX-03
    bool verbose = false;
};

struct BackupStats {
    uint64_t files_ok = 0;
    uint64_t files_skipped = 0;
    uint64_t bytes_copied = 0;
    int exit_code = 0;
};

// Perform a full directory backup (copy mode, no packing)
BackupStats backup(const BackupOptions& opts);

struct RestoreOptions {
    std::string source;   // backup directory
    std::string dest;
    bool verbose = false;
};

struct RestoreStats {
    uint64_t files_restored = 0;
    uint64_t bytes_written = 0;
    int exit_code = 0;
};

// Restore from a backup directory
RestoreStats restore_dir(const RestoreOptions& opts);

}  // namespace cbackup
