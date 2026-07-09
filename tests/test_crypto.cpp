#include <gtest/gtest.h>
#include "crypto/crypto.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cbackup::crypto;

static std::vector<uint8_t> bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

TEST(Rc4, RoundTrip) {
    auto data = bytes("The quick brown fox jumps over the lazy dog");
    std::string key = "secret123";

    auto ct = rc4::crypt(key, data.data(), data.size());
    EXPECT_NE(ct, data);  // actually encrypted

    auto pt = rc4::crypt(key, ct.data(), ct.size());
    EXPECT_EQ(pt, data);
}

TEST(Rc4, LengthPreserved) {
    auto data = bytes("abcdefghij");
    auto ct = rc4::crypt("k", data.data(), data.size());
    EXPECT_EQ(ct.size(), data.size());
}

TEST(Rc4, EmptyKeyThrows) {
    auto data = bytes("x");
    EXPECT_THROW(rc4::crypt("", data.data(), data.size()), std::runtime_error);
}

TEST(Aes, RoundTrip) {
    auto data = bytes("Sensitive backup payload — AES-128-CBC round trip.");
    std::string pw = "correct horse battery staple";

    auto ct = aes::encrypt(pw, data.data(), data.size());
    EXPECT_GT(ct.size(), data.size());  // IV(16) + padding

    auto pt = aes::decrypt(pw, ct.data(), ct.size());
    EXPECT_EQ(pt, data);
}

TEST(Aes, WrongPasswordFails) {
    auto data = bytes("top secret");
    auto ct = aes::encrypt("right-key", data.data(), data.size());
    // Wrong key almost always breaks PKCS#7 padding -> throws.
    EXPECT_THROW(aes::decrypt("wrong-key", ct.data(), ct.size()),
                 std::runtime_error);
}

TEST(Aes, RandomIvProducesDifferentCiphertext) {
    auto data = bytes("same plaintext");
    auto c1 = aes::encrypt("k", data.data(), data.size());
    auto c2 = aes::encrypt("k", data.data(), data.size());
    EXPECT_NE(c1, c2);  // random IV per call
}

TEST(CryptoFactory, Dispatch) {
    auto data = bytes("factory dispatch payload");
    std::string pw = "pw";

    for (auto algo : {Algorithm::RC4, Algorithm::AES128}) {
        auto ct = encrypt(algo, pw, data.data(), data.size());
        auto pt = decrypt(algo, pw, ct.data(), ct.size());
        EXPECT_EQ(pt, data);
    }
}

TEST(CryptoFactory, NoneIsPassthrough) {
    auto data = bytes("plain");
    auto ct = encrypt(Algorithm::NONE, "", data.data(), data.size());
    EXPECT_EQ(ct, data);
}

TEST(CryptoFactory, AlgoFromString) {
    EXPECT_EQ(algo_from_string("rc4"), Algorithm::RC4);
    EXPECT_EQ(algo_from_string("aes"), Algorithm::AES128);
    EXPECT_EQ(algo_from_string("none"), Algorithm::NONE);
    EXPECT_EQ(algo_from_string("xyz"), Algorithm::NONE);
}
