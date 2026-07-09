#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace cbackup {
namespace crypto {

// Encryption algorithm selected at runtime (factory pattern, EX-06).
enum class Algorithm : uint8_t {
    NONE   = 0,
    RC4    = 1,  // hand-implemented stream cipher (10 pts)
    AES128 = 2,  // OpenSSL AES-128-CBC (5 pts)
};

// Encrypt `data` with `password`. The returned buffer is self-describing:
//   - RC4    : ciphertext only (length preserved).
//   - AES128 : [16-byte random IV][CBC ciphertext with PKCS#7 padding].
// Throws std::runtime_error on failure.
std::vector<uint8_t> encrypt(Algorithm algo, const std::string& password,
                             const uint8_t* data, size_t len);

// Inverse of encrypt(). Throws std::runtime_error on failure / bad key.
std::vector<uint8_t> decrypt(Algorithm algo, const std::string& password,
                             const uint8_t* data, size_t len);

// Parse "none" | "rc4" | "aes" (case-insensitive). Unknown -> NONE.
Algorithm algo_from_string(const std::string& s);
const char* algo_name(Algorithm a);

// --- Individual primitives (exposed for unit testing) ---
namespace rc4 {
// Symmetric: encrypt == decrypt. KSA + PRGA + XOR keystream.
std::vector<uint8_t> crypt(const std::string& key,
                           const uint8_t* data, size_t len);
}  // namespace rc4

namespace aes {
// AES-128-CBC via OpenSSL EVP. Output = IV(16) || ciphertext.
std::vector<uint8_t> encrypt(const std::string& password,
                             const uint8_t* data, size_t len);
std::vector<uint8_t> decrypt(const std::string& password,
                             const uint8_t* data, size_t len);
}  // namespace aes

}  // namespace crypto
}  // namespace cbackup
