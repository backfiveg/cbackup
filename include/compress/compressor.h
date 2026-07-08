#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace cbackup {
namespace compress {

constexpr size_t CHUNK_SIZE = 64 * 1024;  // 64KB chunks

// Compress src_buf into dst_buf using zlib deflate
// Returns compressed size, or 0 on failure
std::vector<uint8_t> compress_chunk(const uint8_t* data, size_t len);

// Decompress src_buf (zlib inflate) into original data
// orig_len: expected original size
std::vector<uint8_t> decompress_chunk(const uint8_t* data, size_t len, size_t orig_len);

}  // namespace compress
}  // namespace cbackup
