#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace cbackup {
namespace compress {
namespace huffman {

// Hand-implemented (from scratch) Huffman codec.
//
// Serialized format of an encoded buffer:
//   [uint32 symbol_count]                 number of distinct symbols
//   [ (uint8 sym, uint32 freq) * count ]  frequency table
//   [uint64 bit_count]                    number of valid bits in payload
//   [ bit-packed code stream ]            MSB-first bit packing
//
// encode() returns the serialized buffer. On empty input returns {}.
std::vector<uint8_t> encode(const uint8_t* data, size_t len);

// decode() rebuilds the tree from the embedded frequency table and expands
// exactly orig_len bytes. Throws std::runtime_error on corruption.
std::vector<uint8_t> decode(const uint8_t* data, size_t len, size_t orig_len);

}  // namespace huffman
}  // namespace compress
}  // namespace cbackup
