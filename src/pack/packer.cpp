#include "pack/packer.h"
#include "compress/compressor.h"
#include "filter/filter.h"
#include "utils/logger.h"
#include "utils/path_utils.h"

#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
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

    std::ofstream ofs(opts.dest, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        Logger::error("Cannot create archive: " + opts.dest);
        stats.exit_code = 1;
        return stats;
    }

    // Write archive header
    ArchiveHeader ahdr{};
    ahdr.magic      = PACK_MAGIC;
    ahdr.version    = PACK_VERSION;
    ahdr.compressed = opts.compress ? 1 : 0;
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

            // Apply filter (not for directories)
            if (!S_ISDIR(st.st_mode) && !filter.should_include(rel_path)) {
                stats.entries++;  // counted as skipped entry, not written
                continue;
            }

            EntryType etype = classify(st);

            // Hard-link detection: regular files with nlink > 1
            if (etype == EntryType::REGULAR && st.st_nlink > 1) {
                InodeKey key{st.st_dev, st.st_ino};
                auto it = seen_inodes.find(key);
                if (it != seen_inodes.end()) {
                    // This is a hard link to an already-packed file
                    // Payload = the rel_path of the first occurrence
                    const std::string& target = it->second;
                    std::vector<uint8_t> payload(target.begin(), target.end());
                    EntryHeader ehdr = make_entry_header(
                        st,
                        static_cast<uint32_t>(rel_path.size()),
                        payload.size(),
                        payload.size(),
                        EntryType::HARDLINK);
                    try {
                        write_checked(ofs, &ehdr, sizeof(ehdr));
                        write_checked(ofs, rel_path.data(), rel_path.size());
                        write_checked(ofs, payload.data(), payload.size());
                    } catch (const std::exception& e) {
                        Logger::error(e.what());
                        stats.exit_code = 1;
                        closedir(dirp);
                        return stats;
                    }
                    global_checksum ^= adler32_simple(
                        reinterpret_cast<const uint8_t*>(rel_path.data()),
                        rel_path.size());
                    stats.bytes_out += sizeof(ehdr) + rel_path.size() + payload.size();
                    stats.entries++;
                    Logger::info("Hardlink: " + rel_path + " -> " + target);
                    continue;
                } else {
                    seen_inodes[key] = rel_path;
                    // Fall through to pack as REGULAR
                }
            }

            // Collect data payload
            std::vector<uint8_t> payload;
            uint64_t orig_len = 0;

            if (etype == EntryType::SYMLINK && opts.special_files) {
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
                // No payload, just record the directory
            } else if (!opts.special_files) {
                // Skip special files if not enabled
                continue;
            }

            // Compress payload if enabled
            std::vector<uint8_t> final_payload;
            if (opts.compress && !payload.empty()) {
                try {
                    final_payload = compress::compress_chunk(
                        payload.data(), payload.size());
                } catch (const std::exception& e) {
                    Logger::warn("Compress failed for " + rel_path + ": "
                                 + e.what() + " - storing uncompressed");
                    final_payload = payload;
                }
            } else {
                final_payload = payload;
            }

            // Write entry
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

    printf("[DONE] Pack complete: %lu entries, %.2f MB -> %.2f MB\n",
           stats.entries,
           static_cast<double>(stats.bytes_in) / (1024.0 * 1024.0),
           static_cast<double>(stats.bytes_out) / (1024.0 * 1024.0));
    return stats;
}

}  // namespace cbackup
