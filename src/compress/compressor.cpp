#include "compress/compressor.h"
#include "utils/logger.h"
#include <zlib.h>
#include <stdexcept>

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

}  // namespace compress
}  // namespace cbackup
