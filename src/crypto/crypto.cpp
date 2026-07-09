#include "crypto/crypto.h"

#include <cctype>
#include <stdexcept>
#include <string>

namespace cbackup {
namespace crypto {

std::vector<uint8_t> encrypt(Algorithm algo, const std::string& password,
                             const uint8_t* data, size_t len) {
    switch (algo) {
        case Algorithm::RC4:    return rc4::crypt(password, data, len);
        case Algorithm::AES128: return aes::encrypt(password, data, len);
        case Algorithm::NONE:
        default:
            return std::vector<uint8_t>(data, data + len);
    }
}

std::vector<uint8_t> decrypt(Algorithm algo, const std::string& password,
                             const uint8_t* data, size_t len) {
    switch (algo) {
        case Algorithm::RC4:    return rc4::crypt(password, data, len);
        case Algorithm::AES128: return aes::decrypt(password, data, len);
        case Algorithm::NONE:
        default:
            return std::vector<uint8_t>(data, data + len);
    }
}

Algorithm algo_from_string(const std::string& s) {
    std::string t;
    t.reserve(s.size());
    for (char c : s) t.push_back(static_cast<char>(std::tolower(c)));
    if (t == "rc4") return Algorithm::RC4;
    if (t == "aes" || t == "aes128" || t == "aes-128") return Algorithm::AES128;
    return Algorithm::NONE;
}

const char* algo_name(Algorithm a) {
    switch (a) {
        case Algorithm::RC4:    return "rc4";
        case Algorithm::AES128: return "aes128";
        default:                return "none";
    }
}

}  // namespace crypto
}  // namespace cbackup
