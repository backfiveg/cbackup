#include "crypto/crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cstring>
#include <memory>
#include <stdexcept>

namespace cbackup {
namespace crypto {
namespace aes {

namespace {

constexpr int KEY_LEN = 16;    // AES-128
constexpr int IV_LEN  = 16;
constexpr int BLK_LEN = 16;

// RAII wrapper for EVP_CIPHER_CTX (requirement 5.1: leak-free via custom deleter).
struct CtxDeleter {
    void operator()(EVP_CIPHER_CTX* c) const { if (c) EVP_CIPHER_CTX_free(c); }
};
using CtxPtr = std::unique_ptr<EVP_CIPHER_CTX, CtxDeleter>;

// Derive a 16-byte key from the password via SHA-256 (first 16 bytes).
void derive_key(const std::string& password, unsigned char key[KEY_LEN]) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(password.data()),
           password.size(), digest);
    std::memcpy(key, digest, KEY_LEN);
}

}  // namespace

std::vector<uint8_t> encrypt(const std::string& password,
                             const uint8_t* data, size_t len) {
    if (password.empty()) throw std::runtime_error("aes: empty password");

    unsigned char key[KEY_LEN];
    unsigned char iv[IV_LEN];
    derive_key(password, key);
    if (RAND_bytes(iv, IV_LEN) != 1)
        throw std::runtime_error("aes: RAND_bytes failed");

    CtxPtr ctx(EVP_CIPHER_CTX_new());
    if (!ctx) throw std::runtime_error("aes: ctx alloc failed");

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr, key, iv) != 1)
        throw std::runtime_error("aes: EncryptInit failed");

    // Output layout: IV || ciphertext.
    std::vector<uint8_t> out(IV_LEN + len + BLK_LEN);
    std::memcpy(out.data(), iv, IV_LEN);

    int out_len = 0;
    int total = 0;
    if (EVP_EncryptUpdate(ctx.get(), out.data() + IV_LEN, &out_len,
                          data, static_cast<int>(len)) != 1)
        throw std::runtime_error("aes: EncryptUpdate failed");
    total += out_len;

    if (EVP_EncryptFinal_ex(ctx.get(), out.data() + IV_LEN + total, &out_len) != 1)
        throw std::runtime_error("aes: EncryptFinal failed");
    total += out_len;

    out.resize(IV_LEN + static_cast<size_t>(total));
    return out;
}

std::vector<uint8_t> decrypt(const std::string& password,
                             const uint8_t* data, size_t len) {
    if (password.empty()) throw std::runtime_error("aes: empty password");
    if (len < static_cast<size_t>(IV_LEN))
        throw std::runtime_error("aes: ciphertext too short");

    unsigned char key[KEY_LEN];
    derive_key(password, key);
    const unsigned char* iv = data;
    const uint8_t* ct = data + IV_LEN;
    size_t ct_len = len - IV_LEN;

    CtxPtr ctx(EVP_CIPHER_CTX_new());
    if (!ctx) throw std::runtime_error("aes: ctx alloc failed");

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr, key, iv) != 1)
        throw std::runtime_error("aes: DecryptInit failed");

    std::vector<uint8_t> out(ct_len + BLK_LEN);
    int out_len = 0;
    int total = 0;
    if (EVP_DecryptUpdate(ctx.get(), out.data(), &out_len,
                          ct, static_cast<int>(ct_len)) != 1)
        throw std::runtime_error("aes: DecryptUpdate failed");
    total += out_len;

    if (EVP_DecryptFinal_ex(ctx.get(), out.data() + total, &out_len) != 1)
        throw std::runtime_error("aes: DecryptFinal failed (wrong key?)");
    total += out_len;

    out.resize(static_cast<size_t>(total));
    return out;
}

}  // namespace aes
}  // namespace crypto
}  // namespace cbackup
