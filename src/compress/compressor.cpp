#include "compress/compressor.h"
#include "compress/huffman.h"
#include "utils/logger.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace cbackup {
namespace compress {

std::vector<uint8_t> compress_chunk(const uint8_t* data, size_t len) {
    if (len == 0) return {};

    uLongf bound = compressBound(static_cast<uLong>(len));
    std::vector<uint8_t> out(bound);

    uLongf out_len = bound;
    int ret = ::compress2(out.data(), &out_len,
                          data, static_cast<uLong>(len), Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
        throw std::runtime_error("zlib compress2 failed: " + std::to_string(ret));
    }
    out.resize(out_len);
    return out;
}

std::vector<uint8_t> decompress_chunk(const uint8_t* data, size_t len,
                                       size_t orig_len) {
    if (len == 0 || orig_len == 0) return {};

    std::vector<uint8_t> out(orig_len);
    uLongf out_len = static_cast<uLongf>(orig_len);

    int ret = ::uncompress(out.data(), &out_len,
                           data, static_cast<uLong>(len));
    if (ret != Z_OK) {
        throw std::runtime_error("zlib uncompress failed: " + std::to_string(ret));
    }
    out.resize(out_len);
    return out;
}

std::vector<uint8_t> compress_chunk(Algorithm algo,
                                    const uint8_t* data, size_t len) {
    switch (algo) {
        case Algorithm::ZLIB:    return compress_chunk(data, len);
        case Algorithm::HUFFMAN: return huffman::encode(data, len);
        case Algorithm::NONE:
        default:
            return std::vector<uint8_t>(data, data + len);
    }
}

std::vector<uint8_t> decompress_chunk(Algorithm algo,
                                      const uint8_t* data, size_t len,
                                      size_t orig_len) {
    switch (algo) {
        case Algorithm::ZLIB:    return decompress_chunk(data, len, orig_len);
        case Algorithm::HUFFMAN: return huffman::decode(data, len, orig_len);
        case Algorithm::NONE:
        default:
            return std::vector<uint8_t>(data, data + len);
    }
}

Algorithm algo_from_string(const std::string& s) {
    std::string t;
    t.reserve(s.size());
    for (char c : s) t.push_back(static_cast<char>(std::tolower(c)));
    if (t == "zlib")    return Algorithm::ZLIB;
    if (t == "huffman") return Algorithm::HUFFMAN;
    return Algorithm::NONE;
}

const char* algo_name(Algorithm a) {
    switch (a) {
        case Algorithm::ZLIB:    return "zlib";
        case Algorithm::HUFFMAN: return "huffman";
        default:                 return "none";
    }
}

}  // namespace compress
}  // namespace cbackup
