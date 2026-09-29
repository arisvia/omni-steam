#include "EticketClient.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"

#ifndef OMNI_ETICKET_URL
#define OMNI_ETICKET_URL ""
#endif

namespace EticketClient {

namespace {

std::mutex g_mutex;
std::unordered_map<uint32_t, FreshTicketResult> g_cache;
std::unordered_set<uint32_t> g_noOwnerApps;

bool ExtractStringField(std::string_view body, std::string_view key, std::string& out) {
    std::string needle = "\"" + std::string(key) + "\"";
    size_t k = body.find(needle);
    if (k == std::string_view::npos)
        return false;
    size_t colon = body.find(':', k + needle.size());
    if (colon == std::string_view::npos)
        return false;
    size_t q1 = body.find('"', colon + 1);
    if (q1 == std::string_view::npos)
        return false;
    size_t delim = body.find_first_of(",}", colon + 1);
    if (delim != std::string_view::npos && q1 > delim)
        return false;
    size_t q2 = body.find('"', q1 + 1);
    if (q2 == std::string_view::npos)
        return false;
    out = std::string(body.substr(q1 + 1, q2 - q1 - 1));
    return !out.empty();
}

} // namespace

std::string GetEticketUrl() {
    std::string configured = LuaConfig::GetEticketUrl();
    if (!configured.empty()) {
        return configured;
    }
    return std::string(OMNI_ETICKET_URL);
}

std::optional<FreshTicketResult> FetchFreshEticket(uint32_t appId, std::span<const uint8_t> nonce,
                                                   uint64_t existingSteamId) {
    const std::string url = GetEticketUrl();
    if (url.empty()) {
        return std::nullopt;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_noOwnerApps.contains(appId)) {
            return std::nullopt;
        }
        auto it = g_cache.find(appId);
        if (it != g_cache.end()) {
            if (existingSteamId != 0 && it->second.steamId != 0 && it->second.steamId != existingSteamId) {
                g_cache.erase(it);
            } else {
                return it->second;
            }
        }
    }

    std::string nonceHex = OmniPlatform::Encoding::BytesToHex(nonce.data(), nonce.size());
    std::string reqBody = "{\"app_id\":\"" + std::to_string(appId) + "\",\"nonce\":\"" + nonceHex + "\"";
    if (existingSteamId != 0) {
        reqBody += ",\"existing_steam_id\":\"" + std::to_string(existingSteamId) + "\"";
    }
    reqBody += "}";

    spdlog::info("EticketClient: Requesting fresh on-demand Denuvo ticket for AppID {} from {}", appId, url);
    auto resp = OmniPlatform::Http::Post(url, reqBody, "application/json", 8000);
    if (resp.statusCode != 200 || resp.body.empty()) {
        spdlog::warn("EticketClient: Request failed for AppID {} (HTTP {})", appId, resp.statusCode);
        return std::nullopt;
    }

    std::string eticketHex;
    if (!ExtractStringField(resp.body, "eticket", eticketHex)) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_noOwnerApps.insert(appId);
        return std::nullopt;
    }

    FreshTicketResult result;
    result.eticket = OmniPlatform::Encoding::HexToBytes(eticketHex);
    if (result.eticket.empty()) {
        return std::nullopt;
    }

    std::string appTicketHex;
    if (ExtractStringField(resp.body, "appticket", appTicketHex)) {
        result.appticket = OmniPlatform::Encoding::HexToBytes(appTicketHex);
    }

    std::string steamIdStr;
    if (ExtractStringField(resp.body, "steam_id", steamIdStr)) {
        try {
            result.steamId = std::stoull(steamIdStr);
        } catch (...) {
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_cache[appId] = result;
    }
    spdlog::info("EticketClient: Successfully minted fresh eticket for AppID {} ({} bytes)", appId,
                 result.eticket.size());
    return result;
}

void ClearCache() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache.clear();
    g_noOwnerApps.clear();
}

} // namespace EticketClient
