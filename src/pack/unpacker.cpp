#include "pack/packer.h"
#include "compress/compressor.h"
#include "crypto/crypto.h"
#include "utils/logger.h"
#include "utils/path_utils.h"
#include "utils/fd_wrapper.h"

#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
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

    // ── Pre-check dest ──────────────────────────────────────────
    if (path_utils::exists(opts.dest)) {
        if (!path_utils::is_dir(opts.dest)) {
            Logger::error("Dest exists but is not a directory: " + opts.dest);
            stats.exit_code = 1;
            return stats;
        }
        // Check if dest is non-empty — warn the user.
        DIR* check = opendir(opts.dest.c_str());
        if (check) {
            int count = 0;
            struct dirent* e;
            while ((e = readdir(check)) != nullptr) {
                if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
                    count++;
                if (count > 0) break;
            }
            closedir(check);
            if (count > 0)
                Logger::warn("Dest directory is not empty — files may be "
                             "overwritten: " + opts.dest);
        }
    }

    // ── Create staging directory ────────────────────────────────
    // Extract to a temp staging dir; only rename to final dest on
    // success.  This prevents partial results from polluting the
    // destination on failure.
    std::string staging = opts.dest + ".unpack_tmp";
    if (path_utils::exists(staging)) {
        path_utils::rm_dir_recursive(staging);
    }
    if (!path_utils::mkdir_p(staging)) {
        Logger::error("Cannot create staging directory: " + staging);
        stats.exit_code = 1;
        return stats;
    }

    // ── Open archive ────────────────────────────────────────────
    std::ifstream ifs(opts.archive, std::ios::binary);
    if (!ifs) {
        Logger::error("Cannot open archive: " + opts.archive);
        path_utils::rm_dir_recursive(staging);
        stats.exit_code = 1;
        return stats;
    }

    // Read and validate header
    ArchiveHeader ahdr{};
    try {
        read_checked(ifs, &ahdr, sizeof(ahdr), "archive header");
    } catch (const std::exception& e) {
        Logger::error(e.what());
        path_utils::rm_dir_recursive(staging);
        stats.exit_code = 1;
        return stats;
    }

    if (ahdr.magic != PACK_MAGIC) {
        Logger::error("Invalid archive magic number. File may be corrupted.");
        path_utils::rm_dir_recursive(staging);
        stats.exit_code = 1;
        return stats;
    }
    if (ahdr.version != PACK_VERSION) {
        Logger::error("Unsupported archive version.");
        path_utils::rm_dir_recursive(staging);
        stats.exit_code = 1;
        return stats;
    }

    compress::Algorithm calgo =
        static_cast<compress::Algorithm>(ahdr.compress_algo);
    crypto::Algorithm cipher =
        static_cast<crypto::Algorithm>(ahdr.cipher_algo);

    if (cipher != crypto::Algorithm::NONE && opts.password.empty()) {
        Logger::error("Archive is encrypted; a password (-p) is required.");
        path_utils::rm_dir_recursive(staging);
        stats.exit_code = 1;
        return stats;
    }

    std::string norm_staging = path_utils::normalize(staging);

    // ── Unpack entries ──────────────────────────────────────────
    // Use a lambda so every early-return path cleans up staging.
    auto fail = [&](const std::string& msg) -> PackStats {
        Logger::error(msg);
        path_utils::rm_dir_recursive(staging);
        stats.exit_code = 1;
        return stats;
    };

    while (true) {
        std::streampos cur = ifs.tellg();
        ifs.seekg(0, std::ios::end);
        std::streampos end = ifs.tellg();
        ifs.seekg(cur);
        std::streamoff remaining = end - cur;
        if (remaining <= static_cast<std::streamoff>(sizeof(uint32_t))) {
            break;  // Footer checksum — done.
        }

        EntryHeader ehdr{};
        ifs.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
        if (ifs.gcount() == 0) break;
        if (static_cast<size_t>(ifs.gcount()) != sizeof(ehdr)) {
            return fail("Truncated entry header.");
        }

        if (ehdr.name_len == 0 || ehdr.name_len > 4096) {
            return fail("Corrupt entry: invalid name_len=" +
                        std::to_string(ehdr.name_len));
        }

        // Read name
        std::string rel_name(ehdr.name_len, '\0');
        try {
            read_checked(ifs, rel_name.data(), ehdr.name_len, "entry name");
        } catch (const std::exception& e) {
            return fail(e.what());
        }

        // Security: prevent path traversal
        if (rel_name.find("..") != std::string::npos) {
            Logger::warn("Path traversal detected, skipping: " + rel_name);
            ifs.seekg(ehdr.data_len, std::ios::cur);
            continue;
        }

        std::string dst_path = path_utils::join(norm_staging, rel_name);

        // Ensure parent dir exists (before writing the entry itself)
        std::string parent = path_utils::parent_dir(dst_path);
        if (!path_utils::mkdir_p(parent)) {
            return fail("Cannot create parent directory for: " + rel_name);
        }

        // Read payload
        std::vector<uint8_t> payload(ehdr.data_len);
        if (ehdr.data_len > 0) {
            try {
                read_checked(ifs, payload.data(), ehdr.data_len, rel_name);
            } catch (const std::exception& e) {
                return fail(e.what());
            }
        }

        // Reverse pipeline: decrypt -> decompress.
        std::vector<uint8_t> final_data;
        if (ehdr.data_len > 0) {
            std::vector<uint8_t> decrypted;
            if (cipher != crypto::Algorithm::NONE) {
                try {
                    decrypted = crypto::decrypt(
                        cipher, opts.password, payload.data(), payload.size());
                } catch (const std::exception& e) {
                    return fail("Decrypt failed for " + rel_name + ": "
                                + e.what() + " (wrong password?)");
                }
            } else {
                decrypted = std::move(payload);
            }

            if (calgo != compress::Algorithm::NONE && ehdr.orig_len > 0) {
                try {
                    final_data = compress::decompress_chunk(
                        calgo, decrypted.data(), decrypted.size(),
                        ehdr.orig_len);
                } catch (const std::exception& e) {
                    Logger::warn("Decompress failed for " + rel_name + ": "
                                 + e.what());
                    final_data = decrypted;
                }
            } else {
                final_data = std::move(decrypted);
            }
        }

        // ── Restore by type ──────────────────────────────────
        bool entry_ok = true;

        switch (ehdr.type) {
            case EntryType::DIRECTORY:
                if (!path_utils::mkdir_p(dst_path)) {
                    Logger::warn("Cannot create directory: " + dst_path);
                    entry_ok = false;
                }
                break;

            case EntryType::REGULAR: {
                FdWrapper fd(open(dst_path.c_str(),
                                  O_WRONLY | O_CREAT | O_TRUNC,
                                  ehdr.mode & 07777));
                if (!fd.valid()) {
                    Logger::warn("Cannot create: " + dst_path);
                    entry_ok = false;
                } else if (!final_data.empty()) {
                    ssize_t written_total = 0;
                    size_t to_write = final_data.size();
                    while (written_total < static_cast<ssize_t>(to_write)) {
                        ssize_t w = write(fd.get(),
                                          final_data.data() + written_total,
                                          to_write - written_total);
                        if (w <= 0) {
                            Logger::error("Write failed for " + dst_path
                                          + ": " + strerror(errno));
                            entry_ok = false;
                            break;
                        }
                        written_total += w;
                    }
                    if (entry_ok) {
                        stats.bytes_out += written_total;
                    }
                }
                break;
            }

            case EntryType::SYMLINK: {
                std::string target(final_data.begin(), final_data.end());
                if (symlink(target.c_str(), dst_path.c_str()) != 0) {
                    Logger::warn("symlink failed: " + dst_path
                                 + " -> " + target + ": " + strerror(errno));
                    entry_ok = false;
                }
                break;
            }

            case EntryType::FIFO:
                if (mkfifo(dst_path.c_str(), ehdr.mode & 07777) != 0) {
                    Logger::warn("mkfifo failed: " + dst_path
                                 + ": " + strerror(errno));
                    entry_ok = false;
                }
                break;

            case EntryType::CHR_DEV:
            case EntryType::BLK_DEV: {
                dev_t dev_num = makedev(ehdr.dev_major, ehdr.dev_minor);
                if (mknod(dst_path.c_str(), ehdr.mode, dev_num) != 0) {
                    Logger::warn("mknod failed: " + dst_path
                                 + ": " + strerror(errno));
                    entry_ok = false;
                }
                break;
            }

            case EntryType::HARDLINK: {
                std::string target_rel(final_data.begin(), final_data.end());
                std::string target_path = path_utils::join(norm_staging,
                                                           target_rel);
                if (link(target_path.c_str(), dst_path.c_str()) != 0) {
                    Logger::warn("hard link failed: " + dst_path
                                 + " -> " + target_path
                                 + ": " + strerror(errno));
                    entry_ok = false;
                }
                break;
            }
        }

        // ── Restore metadata ─────────────────────────────────
        if (entry_ok && ehdr.type != EntryType::SYMLINK) {
            chown(dst_path.c_str(), ehdr.uid, ehdr.gid);
            chmod(dst_path.c_str(), ehdr.mode & 07777);

            struct timespec times[2];
            times[0].tv_sec  = ehdr.atime_sec;
            times[0].tv_nsec = ehdr.atime_nsec;
            times[1].tv_sec  = ehdr.mtime_sec;
            times[1].tv_nsec = ehdr.mtime_nsec;
            utimensat(AT_FDCWD, dst_path.c_str(), times, 0);
        } else if (entry_ok && ehdr.type == EntryType::SYMLINK) {
            lchown(dst_path.c_str(), ehdr.uid, ehdr.gid);
        }

        if (entry_ok) {
            stats.entries++;
            Logger::info("Unpacked: " + rel_name);
        }
    }

    // ── Finalise: rename staging → dest ─────────────────────────
    if (path_utils::exists(opts.dest)) {
        // dest is an existing (empty or warned-about) directory.
        // Try rmdir first — it only succeeds when empty, which is
        // the expected case after our pre-check warning.
        rmdir(opts.dest.c_str());
        if (path_utils::exists(opts.dest)) {
            // Non-empty — cannot atomically replace.  Report error
            // but leave staging intact for manual recovery.
            Logger::error("Cannot replace non-empty dest: " + opts.dest
                          + " — staged content is at: " + staging);
            stats.exit_code = 1;
            return stats;
        }
    }

    if (rename(staging.c_str(), opts.dest.c_str()) != 0) {
        Logger::error("Cannot rename staging to dest: " + opts.dest
                      + " (" + strerror(errno)
                      + ") — staged content is at: " + staging);
        stats.exit_code = 1;
        return stats;
    }

    printf("[DONE] Unpack complete: %lu entries, %.2f MB\n",
           stats.entries,
           static_cast<double>(stats.bytes_out) / (1024.0 * 1024.0));
    return stats;
}

}  // namespace cbackup
