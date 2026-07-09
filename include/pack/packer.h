#pragma once
#include <cstdint>
#include <string>
#include <cstring>
#include <sys/stat.h>

#include "filter/filter.h"

namespace cbackup {

// Magic number: "CBKP"
static constexpr uint32_t PACK_MAGIC   = 0x43424B50U;
static constexpr uint16_t PACK_VERSION = 0x0100U;

// File type stored in archive
enum class EntryType : uint8_t {
    REGULAR   = 0,
    DIRECTORY = 1,
    SYMLINK   = 2,
    HARDLINK  = 3,
    FIFO      = 4,
    CHR_DEV   = 5,
    BLK_DEV   = 6,
};

#pragma pack(push, 1)
struct ArchiveHeader {
    uint32_t magic;
    uint16_t version;
    uint8_t  compressed;   // 1 if payload is zlib-compressed
    uint8_t  reserved[9];
};

struct EntryHeader {
    EntryType type;
    uint8_t   reserved[3];
    uint32_t  name_len;
    uint64_t  data_len;          // compressed or raw size on disk
    uint64_t  orig_len;          // original uncompressed size (same if not compressed)
    // metadata (EX-02)
    uint32_t  uid;
    uint32_t  gid;
    uint32_t  mode;
    int64_t   atime_sec;
    int64_t   atime_nsec;
    int64_t   mtime_sec;
    int64_t   mtime_nsec;
    // device info for special files
    uint64_t  dev_major;
    uint64_t  dev_minor;
};
#pragma pack(pop)

struct PackOptions {
    std::string source;
    std::string dest;          // output .cbk file path
    bool compress = false;     // EX-05
    bool preserve_metadata = false;  // EX-02
    bool special_files = false;      // EX-01
    FilterConfig* filter = nullptr;  // EX-03
    bool verbose = false;
};

struct UnpackOptions {
    std::string archive;
    std::string dest;
    bool verbose = false;
};

struct PackStats {
    uint64_t entries = 0;
    uint64_t bytes_in = 0;
    uint64_t bytes_out = 0;
    int exit_code = 0;
};

PackStats pack(const PackOptions& opts);
PackStats unpack(const UnpackOptions& opts);

}  // namespace cbackup
