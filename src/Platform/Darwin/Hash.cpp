#include <CommonCrypto/CommonDigest.h>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

// ponytail: SHA-256 only; no streaming/HMAC is needed here. CommonCrypto lives
// in libSystem (universal), so fat arm64+x86_64 builds need no OpenSSL at all.
// If more digests/HMAC are ever required, add them here rather than reintroducing
// a Homebrew keg dependency that only carries one architecture.
namespace OmniPlatform {
namespace {
std::string ToHex(const uint8_t (&hash)[CC_SHA256_DIGEST_LENGTH]) {
    std::ostringstream oss;
    for (uint8_t byte : hash) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}
} // namespace

std::string Hash::Sha256(const std::vector<uint8_t>& data) {
    uint8_t hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.data(), static_cast<CC_LONG>(data.size()), hash);
    return ToHex(hash);
}

std::string Hash::Sha256File(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file)
        return "";

    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    char buf[4096];
    while (file.read(buf, sizeof(buf))) {
        CC_SHA256_Update(&ctx, buf, static_cast<CC_LONG>(file.gcount()));
    }
    if (file.gcount() > 0) {
        CC_SHA256_Update(&ctx, buf, static_cast<CC_LONG>(file.gcount()));
    }

    uint8_t hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(hash, &ctx);
    return ToHex(hash);
}

} // namespace OmniPlatform
