#include "AppTicket.h"

#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"

namespace AppTicket {

static CacheTicketProviderFn g_cacheTicketProvider = nullptr;

void SetCacheTicketProvider(CacheTicketProviderFn fn) {
    g_cacheTicketProvider = fn;
}

uint64_t ExtractSteamIdFromTicket(const std::vector<uint8_t>& ticket) {
    // Valve AppOwnershipTicket binary format:
    // [uint32 Size][uint32 Version][uint64 SteamID][...]
    if (ticket.size() < kSteamIdTicketMinimumSize) {
        return 0;
    }
    uint64_t steamId = 0;
    std::memcpy(&steamId, ticket.data() + 8, sizeof(uint64_t));
    return steamId;
}

uint64_t GetSpoofSteamID(uint32_t appId) {
    if (!LuaConfig::HasApp(appId) && !LuaConfig::HasDepot(appId)) {
        return 0;
    }
    std::string ticketHex = OmniPlatform::CredentialStore::ReadTicket(appId, "AppTicket");
    if (ticketHex.empty()) {
        ticketHex = OmniPlatform::CredentialStore::ReadTicket(appId, "ETicket");
    }
    if (ticketHex.empty()) {
        return 0;
    }
    auto rawTicket = OmniPlatform::Encoding::HexToBytes(ticketHex);
    uint64_t steamId = ExtractSteamIdFromTicket(rawTicket);
    if (steamId != 0) {
        spdlog::debug("AppTicket: GetSpoofSteamID for AppID {} -> 0x{:X}", appId, steamId);
    }
    return steamId;
}

std::vector<uint8_t> ForgeLocalAppOwnershipTicket(uint32_t appId) {
    std::vector<uint8_t> source =
        g_cacheTicketProvider ? g_cacheTicketProvider(kLocalAppTicketSourceAppId) : std::vector<uint8_t>{};
    if (source.size() <= kAppTicketSignatureSize) {
        spdlog::debug("AppTicket: ForgeLocalAppOwnershipTicket for AppID {}: source ticket for App 7 not found", appId);
        return {};
    }

    const size_t signedSize = source.size() - kAppTicketSignatureSize;
    std::vector<uint8_t> ticket;
    ticket.reserve(source.size() + sizeof(uint32_t));
    ticket.insert(ticket.end(), source.begin(), source.begin() + signedSize);

    const auto* appIdBytes = reinterpret_cast<const uint8_t*>(&appId);
    ticket.insert(ticket.end(), appIdBytes, appIdBytes + sizeof(uint32_t));
    ticket.insert(ticket.end(), source.begin() + signedSize, source.end());

    spdlog::info("AppTicket: Forged AppOwnershipTicket for AppID {} using AppID 7 source (size: {} -> {})", appId,
                 source.size(), ticket.size());
    return ticket;
}

bool GetAppOwnershipTicket(uint32_t appId, AppOwnershipTicket& outTicket) {
    outTicket = {};

    // 1. Try imported credential store ticket first
    std::string ticketHex = OmniPlatform::CredentialStore::ReadTicket(appId, "AppTicket");
    if (!ticketHex.empty()) {
        outTicket.data = OmniPlatform::Encoding::HexToBytes(ticketHex);
        if (outTicket.data.size() >= sizeof(uint32_t)) {
            outTicket.totalSize = static_cast<uint32_t>(outTicket.data.size());
            outTicket.appIdOffset = kAppTicketAppIdOffset;
            outTicket.steamIdOffset = kAppTicketSteamIdOffset;
            std::memcpy(&outTicket.signatureOffset, outTicket.data.data(), sizeof(uint32_t));
            outTicket.signatureSize = static_cast<uint32_t>(kAppTicketSignatureSize);
            return true;
        }
    }

    // 2. Fall back to local CEG AppID 7 forgery
    outTicket.data = ForgeLocalAppOwnershipTicket(appId);
    if (outTicket.data.empty()) {
        return false;
    }

    outTicket.totalSize = static_cast<uint32_t>(outTicket.data.size() - sizeof(uint32_t));
    outTicket.appIdOffset = outTicket.totalSize - static_cast<uint32_t>(kAppTicketSignatureSize);
    outTicket.steamIdOffset = kAppTicketSteamIdOffset;
    outTicket.signatureOffset = outTicket.appIdOffset + sizeof(uint32_t);
    outTicket.signatureSize = static_cast<uint32_t>(kAppTicketSignatureSize);
    return true;
}

} // namespace AppTicket
