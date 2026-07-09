#include "crypto/crypto.h"

#include <array>
#include <stdexcept>

namespace cbackup {
namespace crypto {
namespace rc4 {

// Hand-implemented RC4 stream cipher.
//   KSA  : initialise the 256-byte S-box from the key.
//   PRGA : generate the keystream and XOR it with the data.
// Being a symmetric XOR cipher, the same routine encrypts and decrypts.
std::vector<uint8_t> crypt(const std::string& key,
                           const uint8_t* data, size_t len) {
    if (key.empty())
        throw std::runtime_error("rc4: empty key");
    if (len == 0 || data == nullptr) return {};

    // --- KSA (Key Scheduling Algorithm) ---
    std::array<uint8_t, 256> S;
    for (int i = 0; i < 256; ++i)
        S[static_cast<size_t>(i)] = static_cast<uint8_t>(i);

    int j = 0;
    const size_t klen = key.size();
    for (int i = 0; i < 256; ++i) {
        j = (j + S[static_cast<size_t>(i)]
                + static_cast<uint8_t>(key[static_cast<size_t>(i) % klen])) & 0xFF;
        std::swap(S[static_cast<size_t>(i)], S[static_cast<size_t>(j)]);
    }

    // --- PRGA (Pseudo-Random Generation Algorithm) ---
    std::vector<uint8_t> out(len);
    int a = 0, b = 0;
    for (size_t n = 0; n < len; ++n) {
        a = (a + 1) & 0xFF;
        b = (b + S[static_cast<size_t>(a)]) & 0xFF;
        std::swap(S[static_cast<size_t>(a)], S[static_cast<size_t>(b)]);
        uint8_t k = S[(S[static_cast<size_t>(a)]
                       + S[static_cast<size_t>(b)]) & 0xFF];
        out[n] = static_cast<uint8_t>(data[n] ^ k);
    }
    return out;
}

}  // namespace rc4
}  // namespace crypto
}  // namespace cbackup
