#include <cstdint>
#include <fstream>
#include <iomanip>
#include <openssl/evp.h>
#include <sstream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

std::string Hash::Sha256(const std::vector<uint8_t>& data) {
    uint8_t hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
        return "";

    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, hash, &length);
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < length; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string Hash::Sha256File(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file)
        return "";

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
        return "";

    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    char buf[4096];
    while (file.read(buf, sizeof(buf))) {
        EVP_DigestUpdate(ctx, buf, file.gcount());
    }
    if (file.gcount() > 0) {
        EVP_DigestUpdate(ctx, buf, file.gcount());
    }

    uint8_t hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    EVP_DigestFinal_ex(ctx, hash, &length);
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < length; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

} // namespace OmniPlatform
