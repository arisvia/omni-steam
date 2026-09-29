#include "LegacyCDKey.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>

#include "Utils/Config/LuaConfig.h"

namespace LegacyCDKey {

namespace {

// Unambiguous 32-char alphabet (Crockford base32: no I, L, O, U).
constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

uint32_t Fnv1a32(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}

} // namespace

std::string Synthesize(uint32_t appId, uint32_t accountId) {
    char seedA[32];
    std::snprintf(seedA, sizeof(seedA), "%u:%u", appId, accountId);
    char seedB[40];
    std::snprintf(seedB, sizeof(seedB), "OMNI:%u:%u", accountId, appId);

    uint64_t bits = (static_cast<uint64_t>(Fnv1a32(seedA)) << 32) | Fnv1a32(seedB);

    std::string key;
    key.reserve(19);
    for (int i = 0; i < 16; ++i) {
        if (i && (i % 4) == 0) {
            key.push_back('-');
        }
        key.push_back(kAlphabet[bits & 31]);
        bits = (bits >> 4) | (bits << 60); // rotate so every char draws fresh bits
    }
    return key;
}

std::optional<std::string> Resolve(uint32_t appId, uint32_t accountId) {
    if (!LuaConfig::HasApp(appId) && !LuaConfig::HasDepot(appId)) {
        return std::nullopt;
    }

    auto overrideKey = LuaConfig::GetLegacyCDKey(appId);
    if (overrideKey && !overrideKey->empty()) {
        spdlog::info("LegacyCDKey: Resolved custom override for AppID {}", appId);
        return overrideKey;
    }

    std::string synthesized = Synthesize(appId, accountId);
    spdlog::info("LegacyCDKey: Synthesized deterministic key {} for AppID {} (AccountID {})", synthesized, appId,
                 accountId);
    return synthesized;
}

} // namespace LegacyCDKey
