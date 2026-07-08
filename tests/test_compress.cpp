#include <gtest/gtest.h>
#include "compress/compressor.h"

#include <string>
#include <vector>

using namespace cbackup::compress;

TEST(Compress, RoundTrip) {
    std::string original = "Hello, this is a test string for compression! "
                           "It should compress and decompress losslessly.";
    std::vector<uint8_t> data(original.begin(), original.end());

    auto compressed = compress_chunk(data.data(), data.size());
    EXPECT_FALSE(compressed.empty());
    EXPECT_LT(compressed.size(), data.size() * 2);  // Not exploding

    auto decompressed = decompress_chunk(compressed.data(), compressed.size(),
                                         data.size());
    ASSERT_EQ(decompressed.size(), data.size());
    EXPECT_EQ(std::string(decompressed.begin(), decompressed.end()), original);
}

TEST(Compress, EmptyInput) {
    auto compressed = compress_chunk(nullptr, 0);
    EXPECT_TRUE(compressed.empty());

    auto decompressed = decompress_chunk(nullptr, 0, 0);
    EXPECT_TRUE(decompressed.empty());
}

TEST(Compress, LargeData) {
    // Generate 256KB of repetitive data
    std::vector<uint8_t> big_data(256 * 1024, 'A');
    for (size_t i = 0; i < big_data.size(); i += 16)
        big_data[i] = static_cast<uint8_t>(i & 0xFF);

    auto compressed = compress_chunk(big_data.data(), big_data.size());
    EXPECT_FALSE(compressed.empty());
    // Repetitive data should compress well
    EXPECT_LT(compressed.size(), big_data.size());

    auto decompressed = decompress_chunk(compressed.data(), compressed.size(),
                                          big_data.size());
    EXPECT_EQ(decompressed, big_data);
}

TEST(Compress, DecompressWrongSizeThrows) {
    std::string s = "test data for compression";
    std::vector<uint8_t> d(s.begin(), s.end());
    auto c = compress_chunk(d.data(), d.size());

    // Providing wrong orig_len should throw or return wrong data
    EXPECT_THROW(
        decompress_chunk(c.data(), c.size(), d.size() * 100),
        std::runtime_error
    );
}
