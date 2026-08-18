#include "OmniPlatform/OmniPlatform.h"
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace OmniPlatform {

std::string Hash::Sha256(const std::vector<uint8_t>& data) {
    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(data.data(), data.size(), hash);
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string Hash::Sha256File(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return "";
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    char buf[4096];
    while (file.read(buf, sizeof(buf))) SHA256_Update(&ctx, buf, file.gcount());
    if (file.gcount() > 0) SHA256_Update(&ctx, buf, file.gcount());
    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string Hash::Md5(const std::vector<uint8_t>& data) {
    uint8_t hash[MD5_DIGEST_LENGTH];
    MD5(data.data(), data.size(), hash);
    std::ostringstream oss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

} // namespace OmniPlatform
