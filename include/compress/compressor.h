#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cbackup {
namespace compress {

constexpr size_t CHUNK_SIZE = 64 * 1024;  // 64KB chunks

// Compression algorithm selected at runtime (factory pattern, EX-05).
enum class Algorithm : uint8_t {
    NONE    = 0,
    ZLIB    = 1,  // third-party zlib (5 pts)
    HUFFMAN = 2,  // hand-implemented Huffman (10 pts)
};

// --- Legacy zlib free functions (kept for backward compatibility) ---
// Compress src buffer using zlib deflate. Returns compressed bytes.
std::vector<uint8_t> compress_chunk(const uint8_t* data, size_t len);
// Decompress a zlib buffer; orig_len is the expected original size.
std::vector<uint8_t> decompress_chunk(const uint8_t* data, size_t len,
                                      size_t orig_len);

// --- Factory dispatch: pick algorithm at runtime ---
std::vector<uint8_t> compress_chunk(Algorithm algo,
                                    const uint8_t* data, size_t len);
std::vector<uint8_t> decompress_chunk(Algorithm algo,
                                      const uint8_t* data, size_t len,
                                      size_t orig_len);

// Parse "none" | "zlib" | "huffman" (case-insensitive). Unknown -> NONE.
Algorithm algo_from_string(const std::string& s);
const char* algo_name(Algorithm a);

}  // namespace compress
}  // namespace cbackup
