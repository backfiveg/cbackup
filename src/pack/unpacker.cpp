#include "pack/packer.h"
#include "compress/compressor.h"
#include "utils/logger.h"
#include "utils/path_utils.h"
#include "utils/fd_wrapper.h"

#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace cbackup {

static void read_checked(std::ifstream& ifs, void* buf, size_t sz,
                          const std::string& ctx) {
    ifs.read(reinterpret_cast<char*>(buf), sz);
    if (static_cast<size_t>(ifs.gcount()) != sz)
        throw std::runtime_error("Unexpected EOF reading " + ctx);
}

PackStats unpack(const UnpackOptions& opts) {
    PackStats stats;

    if (!path_utils::validate_path(opts.archive) ||
        !path_utils::validate_path(opts.dest)) {
        Logger::error("Invalid path.");
        stats.exit_code = 1;
        return stats;
    }

    if (!path_utils::exists(opts.archive)) {
        Logger::error("Archive not found: " + opts.archive);
        stats.exit_code = 1;
        return stats;
    }

    if (!path_utils::mkdir_p(opts.dest)) {
        Logger::error("Cannot create dest: " + opts.dest);
        stats.exit_code = 1;
        return stats;
    }

    std::ifstream ifs(opts.archive, std::ios::binary);
    if (!ifs) {
        Logger::error("Cannot open archive: " + opts.archive);
        stats.exit_code = 1;
        return stats;
    }

    // Read and validate header
    ArchiveHeader ahdr{};
    try {
        read_checked(ifs, &ahdr, sizeof(ahdr), "archive header");
    } catch (const std::exception& e) {
        Logger::error(e.what());
        stats.exit_code = 1;
        return stats;
    }

    if (ahdr.magic != PACK_MAGIC) {
        Logger::error("Invalid archive magic number. File may be corrupted.");
        stats.exit_code = 1;
        return stats;
    }
    if (ahdr.version != PACK_VERSION) {
        Logger::error("Unsupported archive version.");
        stats.exit_code = 1;
        return stats;
    }

    bool compressed = (ahdr.compressed == 1);
    std::string norm_dst = path_utils::normalize(opts.dest);

    // Read entries until we reach the 4-byte footer checksum at end of file
    while (true) {
        // Peek at how many bytes remain; if <= sizeof(uint32_t) it's the footer
        std::streampos cur = ifs.tellg();
        ifs.seekg(0, std::ios::end);
        std::streampos end = ifs.tellg();
        ifs.seekg(cur);
        std::streamoff remaining = end - cur;
        if (remaining <= static_cast<std::streamoff>(sizeof(uint32_t))) {
            // Footer checksum — we're done
            break;
        }

        EntryHeader ehdr{};
        ifs.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
        if (ifs.gcount() == 0) break;  // EOF
        if (static_cast<size_t>(ifs.gcount()) != sizeof(ehdr)) {
            Logger::error("Truncated entry header.");
            stats.exit_code = 1;
            return stats;
        }

        // Safety: cap name_len to avoid huge allocation
        if (ehdr.name_len == 0 || ehdr.name_len > 4096) {
            Logger::error("Corrupt entry: invalid name_len=" +
                          std::to_string(ehdr.name_len));
            stats.exit_code = 1;
            return stats;
        }

        // Read name
        std::string rel_name(ehdr.name_len, '\0');
        try {
            read_checked(ifs, rel_name.data(), ehdr.name_len, "entry name");
        } catch (const std::exception& e) {
            Logger::error(e.what());
            stats.exit_code = 1;
            return stats;
        }

        // Security: prevent path traversal
        if (rel_name.find("..") != std::string::npos) {
            Logger::warn("Path traversal detected, skipping: " + rel_name);
            ifs.seekg(ehdr.data_len, std::ios::cur);
            continue;
        }

        std::string dst_path = path_utils::join(norm_dst, rel_name);
        // Ensure parent dir exists
        path_utils::mkdir_p(path_utils::parent_dir(dst_path));

        // Read payload
        std::vector<uint8_t> payload(ehdr.data_len);
        if (ehdr.data_len > 0) {
            try {
                read_checked(ifs, payload.data(), ehdr.data_len, rel_name);
            } catch (const std::exception& e) {
                Logger::error(e.what());
                stats.exit_code = 1;
                return stats;
            }
        }

        // Decompress if needed
        std::vector<uint8_t> final_data;
        if (compressed && ehdr.orig_len > 0 && ehdr.data_len > 0) {
            try {
                final_data = compress::decompress_chunk(
                    payload.data(), payload.size(), ehdr.orig_len);
            } catch (const std::exception& e) {
                Logger::warn("Decompress failed for " + rel_name + ": " + e.what());
                final_data = payload;
            }
        } else {
            final_data = payload;
        }

        // Restore by type
        switch (ehdr.type) {
            case EntryType::DIRECTORY:
                path_utils::mkdir_p(dst_path);
                break;

            case EntryType::REGULAR: {
                FdWrapper fd(open(dst_path.c_str(),
                                  O_WRONLY | O_CREAT | O_TRUNC,
                                  ehdr.mode & 07777));
                if (!fd.valid()) {
                    Logger::warn("Cannot create: " + dst_path);
                    stats.entries++;
                    continue;
                }
                if (!final_data.empty()) {
                    write(fd.get(), final_data.data(), final_data.size());
                }
                stats.bytes_out += final_data.size();
                break;
            }

            case EntryType::SYMLINK: {
                std::string target(final_data.begin(), final_data.end());
                symlink(target.c_str(), dst_path.c_str());
                break;
            }

            case EntryType::FIFO:
                mkfifo(dst_path.c_str(), ehdr.mode & 07777);
                break;

            case EntryType::CHR_DEV:
            case EntryType::BLK_DEV: {
                dev_t dev_num = makedev(ehdr.dev_major, ehdr.dev_minor);
                mknod(dst_path.c_str(), ehdr.mode, dev_num);
                break;
            }

            case EntryType::HARDLINK: {
                // payload is the rel_path of the link target
                std::string target_rel(final_data.begin(), final_data.end());
                std::string target_path = path_utils::join(norm_dst, target_rel);
                if (link(target_path.c_str(), dst_path.c_str()) != 0) {
                    Logger::warn("hard link failed: " + dst_path
                                 + " -> " + target_path
                                 + ": " + strerror(errno));
                }
                break;
            }
        }

        // Restore metadata (uid/gid/mode/timestamps)
        if (ehdr.type != EntryType::SYMLINK) {
            chown(dst_path.c_str(), ehdr.uid, ehdr.gid);
            chmod(dst_path.c_str(), ehdr.mode & 07777);

            struct timespec times[2];
            times[0].tv_sec  = ehdr.atime_sec;
            times[0].tv_nsec = ehdr.atime_nsec;
            times[1].tv_sec  = ehdr.mtime_sec;
            times[1].tv_nsec = ehdr.mtime_nsec;
            utimensat(AT_FDCWD, dst_path.c_str(), times, 0);
        } else {
            lchown(dst_path.c_str(), ehdr.uid, ehdr.gid);
        }

        stats.entries++;
        Logger::info("Unpacked: " + rel_name);
    }

    printf("[DONE] Unpack complete: %lu entries, %.2f MB\n",
           stats.entries,
           static_cast<double>(stats.bytes_out) / (1024.0 * 1024.0));
    return stats;
}

}  // namespace cbackup
