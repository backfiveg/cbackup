#include "pack/packer.h"
#include "compress/compressor.h"
#include "crypto/crypto.h"
#include "filter/filter.h"
#include "utils/logger.h"
#include "utils/path_utils.h"

#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <cstdio>
#include <cerrno>
#include <fstream>
#include <map>
#include <stack>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cbackup {

// Compute a simple Adler-32-style checksum
static uint32_t adler32_simple(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static void write_checked(std::ofstream& ofs, const void* data, size_t sz) {
    ofs.write(reinterpret_cast<const char*>(data), sz);
    if (!ofs) throw std::runtime_error("Write error: disk full?");
}

static EntryType classify(const struct stat& st) {
    if (S_ISREG(st.st_mode))  return EntryType::REGULAR;
    if (S_ISDIR(st.st_mode))  return EntryType::DIRECTORY;
    if (S_ISLNK(st.st_mode))  return EntryType::SYMLINK;
    if (S_ISFIFO(st.st_mode)) return EntryType::FIFO;
    if (S_ISCHR(st.st_mode))  return EntryType::CHR_DEV;
    if (S_ISBLK(st.st_mode))  return EntryType::BLK_DEV;
    return EntryType::REGULAR;
}

// Key for hard-link dedup: (dev, ino)
using InodeKey = std::pair<dev_t, ino_t>;

static EntryHeader make_entry_header(const struct stat& st,
                                      uint32_t name_len,
                                      uint64_t data_len,
                                      uint64_t orig_len,
                                      const EntryType& type) {
    EntryHeader hdr{};
    hdr.type        = type;
    hdr.name_len    = name_len;
    hdr.data_len    = data_len;
    hdr.orig_len    = orig_len;
    hdr.uid         = st.st_uid;
    hdr.gid         = st.st_gid;
    hdr.mode        = st.st_mode;
    hdr.atime_sec   = st.st_atim.tv_sec;
    hdr.atime_nsec  = st.st_atim.tv_nsec;
    hdr.mtime_sec   = st.st_mtim.tv_sec;
    hdr.mtime_nsec  = st.st_mtim.tv_nsec;
    hdr.dev_major   = major(st.st_rdev);
    hdr.dev_minor   = minor(st.st_rdev);
    return hdr;
}

PackStats pack(const PackOptions& opts) {
    PackStats stats;

    if (!path_utils::validate_path(opts.source) ||
        !path_utils::validate_path(opts.dest)) {
        Logger::error("Invalid path.");
        stats.exit_code = 1;
        return stats;
    }

    if (!path_utils::is_dir(opts.source)) {
        Logger::error("Source is not a directory: " + opts.source);
        stats.exit_code = 1;
        return stats;
    }

    // Ensure dest parent exists
    std::string dest_parent = path_utils::parent_dir(opts.dest);
    if (!path_utils::mkdir_p(dest_parent)) {
        Logger::error("Cannot create dest dir: " + dest_parent);
        stats.exit_code = 1;
        return stats;
    }

    // Write to a temp file first; only rename to final dest on success.
    // This prevents leaving a corrupt .cbk file on partial failure.
    std::string tmp_dest = opts.dest + ".pack_tmp";
    if (path_utils::exists(tmp_dest)) {
        unlink(tmp_dest.c_str());
    }

    std::ofstream ofs(tmp_dest, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        Logger::error("Cannot create archive: " + tmp_dest);
        stats.exit_code = 1;
        return stats;
    }

    // On any failure after this point, remove the partial temp file.
    auto cleanup_tmp = [&]() {
        ofs.close();
        unlink(tmp_dest.c_str());
    };

    // Resolve effective compression algorithm (legacy -z => zlib).
    compress::Algorithm calgo = opts.compress_algo;
    if (calgo == compress::Algorithm::NONE && opts.compress)
        calgo = compress::Algorithm::ZLIB;
    crypto::Algorithm cipher = opts.cipher_algo;
    if (cipher != crypto::Algorithm::NONE && opts.password.empty()) {
        Logger::error("Encryption requested but no password provided.");
        cleanup_tmp();
        stats.exit_code = 1;
        return stats;
    }

    // Write archive header
    ArchiveHeader ahdr{};
    ahdr.magic         = PACK_MAGIC;
    ahdr.version       = PACK_VERSION;
    ahdr.compress_algo = static_cast<uint8_t>(calgo);
    ahdr.cipher_algo   = static_cast<uint8_t>(cipher);
    write_checked(ofs, &ahdr, sizeof(ahdr));

    std::string norm_src = path_utils::normalize(opts.source);
    Filter filter(opts.filter ? *opts.filter : FilterConfig{});

    // Running checksum
    uint32_t global_checksum = 0;

    // Hard-link dedup: (dev,ino) -> first seen rel_path
    std::map<InodeKey, std::string> seen_inodes;

    // Iterative DFS
    using StackEntry = std::pair<std::string, std::string>;
    std::stack<StackEntry> stk;
    stk.push({norm_src, ""});

    while (!stk.empty()) {
        auto [src_dir, rel_prefix] = stk.top();
        stk.pop();

        DIR* dirp = opendir(src_dir.c_str());
        if (!dirp) {
            Logger::warn("Cannot open: " + src_dir);
            continue;
        }

        struct dirent* ent;
        while ((ent = readdir(dirp)) != nullptr) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;

            std::string src_path = path_utils::join(src_dir, ent->d_name);
            std::string rel_path = rel_prefix.empty() ?
                                   ent->d_name :
                                   rel_prefix + "/" + ent->d_name;

            struct stat st;
            if (lstat(src_path.c_str(), &st) != 0) {
                Logger::warn("lstat: " + src_path);
                continue;
            }

            // Apply 6-dimension filter (not for directories, which we must
            // still descend to preserve structure).
            if (!S_ISDIR(st.st_mode) && !filter.should_include(rel_path, st)) {
                stats.entries++;  // counted as skipped entry, not written
                continue;
            }

            EntryType etype = classify(st);

            // Payload + original length, computed uniformly per entry type.
            std::vector<uint8_t> payload;
            uint64_t orig_len = 0;
            bool is_hardlink = false;

            // Hard-link detection: regular files with nlink > 1.
            if (etype == EntryType::REGULAR && st.st_nlink > 1) {
                InodeKey key{st.st_dev, st.st_ino};
                auto it = seen_inodes.find(key);
                if (it != seen_inodes.end()) {
                    // Hard link to an already-packed file: payload is the
                    // rel_path of the first occurrence.
                    const std::string& target = it->second;
                    payload.assign(target.begin(), target.end());
                    orig_len = payload.size();
                    etype = EntryType::HARDLINK;
                    is_hardlink = true;
                    Logger::info("Hardlink: " + rel_path + " -> " + target);
                } else {
                    seen_inodes[key] = rel_path;
                }
            }

            if (!is_hardlink) {
                if (etype == EntryType::SYMLINK) {
                    if (!opts.special_files) { continue; }
                    char link_buf[PATH_MAX + 1];
                    ssize_t llen = readlink(src_path.c_str(), link_buf, PATH_MAX);
                    if (llen > 0) {
                        link_buf[llen] = '\0';
                        payload.assign(link_buf, link_buf + llen);
                        orig_len = payload.size();
                    }
                } else if (etype == EntryType::REGULAR) {
                    int fd = open(src_path.c_str(), O_RDONLY);
                    if (fd >= 0) {
                        std::vector<uint8_t> tmp(compress::CHUNK_SIZE);
                        ssize_t n;
                        while ((n = read(fd, tmp.data(), tmp.size())) > 0) {
                            payload.insert(payload.end(),
                                           tmp.data(), tmp.data() + n);
                        }
                        close(fd);
                        orig_len = payload.size();
                        stats.bytes_in += orig_len;
                    }
                } else if (etype == EntryType::DIRECTORY) {
                    // No payload, just record the directory node.
                } else {
                    // FIFO / CHR_DEV / BLK_DEV: metadata only, no payload.
                    if (!opts.special_files) { continue; }
                }
            }

            // Uniform pipeline: compress -> encrypt. orig_len stays the
            // ORIGINAL payload size so unpack can size the output correctly.
            std::vector<uint8_t> final_payload;
            if (!payload.empty()) {
                std::vector<uint8_t> comp;
                if (calgo != compress::Algorithm::NONE) {
                    try {
                        comp = compress::compress_chunk(
                            calgo, payload.data(), payload.size());
                    } catch (const std::exception& e) {
                        Logger::warn("Compress failed for " + rel_path + ": "
                                     + e.what() + " - storing uncompressed");
                        comp = payload;
                    }
                } else {
                    comp = payload;
                }

                if (cipher != crypto::Algorithm::NONE) {
                    try {
                        final_payload = crypto::encrypt(
                            cipher, opts.password, comp.data(), comp.size());
                    } catch (const std::exception& e) {
                        Logger::error("Encrypt failed for " + rel_path + ": "
                                      + e.what());
                        cleanup_tmp();
                        stats.exit_code = 1;
                        closedir(dirp);
                        return stats;
                    }
                } else {
                    final_payload = std::move(comp);
                }
            }

            // Write entry: data_len = on-disk size, orig_len = original size.
            EntryHeader ehdr = make_entry_header(
                st,
                static_cast<uint32_t>(rel_path.size()),
                final_payload.size(),
                orig_len,
                etype);

            try {
                write_checked(ofs, &ehdr, sizeof(ehdr));
                write_checked(ofs, rel_path.data(), rel_path.size());
                if (!final_payload.empty())
                    write_checked(ofs, final_payload.data(), final_payload.size());
            } catch (const std::exception& e) {
                Logger::error(e.what());
                cleanup_tmp();
                stats.exit_code = 1;
                closedir(dirp);
                return stats;
            }

            // Update checksum
            global_checksum ^= adler32_simple(
                reinterpret_cast<const uint8_t*>(rel_path.data()),
                rel_path.size());
            stats.bytes_out += sizeof(ehdr) + rel_path.size() + final_payload.size();
            stats.entries++;
            Logger::info("Packed: " + rel_path);

            if (etype == EntryType::DIRECTORY)
                stk.push({src_path, rel_path});
        }
        closedir(dirp);
    }

    // Write footer checksum
    write_checked(ofs, &global_checksum, sizeof(global_checksum));
    ofs.close();
    if (!ofs) {
        Logger::error("Failed to flush archive — disk full?");
        cleanup_tmp();
        stats.exit_code = 1;
        return stats;
    }

    // Atomically rename temp file to final destination.
    if (path_utils::exists(opts.dest)) {
        unlink(opts.dest.c_str());
    }
    if (rename(tmp_dest.c_str(), opts.dest.c_str()) != 0) {
        Logger::error("Cannot rename temp archive to " + opts.dest
                      + ": " + strerror(errno));
        cleanup_tmp();
        stats.exit_code = 1;
        return stats;
    }

    printf("[DONE] Pack complete: %lu entries, %.2f MB -> %.2f MB\n",
           stats.entries,
           static_cast<double>(stats.bytes_in) / (1024.0 * 1024.0),
           static_cast<double>(stats.bytes_out) / (1024.0 * 1024.0));
    return stats;
}

}  // namespace cbackup
