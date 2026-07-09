#include <gtest/gtest.h>
#include "compress/compressor.h"
#include "compress/huffman.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace cbackup::compress;

TEST(Huffman, RoundTripText) {
    std::string s = "the quick brown fox jumps over the lazy dog. "
                    "AAAAAAAAAABBBBBBCCCDDE!!!";
    std::vector<uint8_t> data(s.begin(), s.end());

    auto enc = huffman::encode(data.data(), data.size());
    EXPECT_FALSE(enc.empty());

    auto dec = huffman::decode(enc.data(), enc.size(), data.size());
    ASSERT_EQ(dec.size(), data.size());
    EXPECT_EQ(std::string(dec.begin(), dec.end()), s);
}

TEST(Huffman, EmptyInput) {
    auto enc = huffman::encode(nullptr, 0);
    EXPECT_TRUE(enc.empty());
    auto dec = huffman::decode(nullptr, 0, 0);
    EXPECT_TRUE(dec.empty());
}

TEST(Huffman, SingleSymbol) {
    std::vector<uint8_t> data(1000, 'Z');  // only one distinct byte
    auto enc = huffman::encode(data.data(), data.size());
    auto dec = huffman::decode(enc.data(), enc.size(), data.size());
    EXPECT_EQ(dec, data);
}

TEST(Huffman, CompressesRepetitiveData) {
    // 256KB, highly skewed distribution -> Huffman should shrink it.
    std::vector<uint8_t> data(256 * 1024, 'A');
    for (size_t i = 0; i < data.size(); i += 32) data[i] = 'B';

    auto enc = huffman::encode(data.data(), data.size());
    EXPECT_LT(enc.size(), data.size());

    auto dec = huffman::decode(enc.data(), enc.size(), data.size());
    EXPECT_EQ(dec, data);
}

TEST(Huffman, AllByteValues) {
    std::vector<uint8_t> data;
    for (int i = 0; i < 256; ++i)
        for (int r = 0; r < 4; ++r)
            data.push_back(static_cast<uint8_t>(i));

    auto enc = huffman::encode(data.data(), data.size());
    auto dec = huffman::decode(enc.data(), enc.size(), data.size());
    EXPECT_EQ(dec, data);
}

TEST(Huffman, FactoryDispatch) {
    std::string s = "factory pattern dispatch test payload";
    std::vector<uint8_t> data(s.begin(), s.end());

    auto enc = compress_chunk(Algorithm::HUFFMAN, data.data(), data.size());
    auto dec = decompress_chunk(Algorithm::HUFFMAN, enc.data(), enc.size(),
                                data.size());
    EXPECT_EQ(std::string(dec.begin(), dec.end()), s);
}

TEST(Huffman, AlgoFromString) {
    EXPECT_EQ(algo_from_string("huffman"), Algorithm::HUFFMAN);
    EXPECT_EQ(algo_from_string("ZLIB"), Algorithm::ZLIB);
    EXPECT_EQ(algo_from_string("bogus"), Algorithm::NONE);
}
