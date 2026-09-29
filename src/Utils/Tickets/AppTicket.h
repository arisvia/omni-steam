#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace AppTicket {

constexpr uint32_t kLocalAppTicketSourceAppId = 7;
constexpr size_t kAppTicketSignatureSize = 128; // RSA-1024 signature
constexpr size_t kSteamIdTicketMinimumSize = 16;
constexpr uint32_t kAppTicketAppIdOffset = 8;
constexpr uint32_t kAppTicketSteamIdOffset = 16;

struct AppOwnershipTicket {
    std::vector<uint8_t> data;
    uint32_t totalSize = 0;
    uint32_t appIdOffset = 0;
    uint32_t steamIdOffset = 0;
    uint32_t signatureOffset = 0;
    uint32_t signatureSize = 0;
};

// Extracts 64-bit SteamID from an AppOwnershipTicket binary payload
uint64_t ExtractSteamIdFromTicket(const std::vector<uint8_t>& ticket);

// Returns spoofed SteamID from ticket in CredentialStore for the app (0 if none)
uint64_t GetSpoofSteamID(uint32_t appId);

// Forges a local AppOwnershipTicket for legacy SteamDRM/CEG games
// by exploiting the 4-byte offset parsing loophole with AppID 7 signed ticket
std::vector<uint8_t> ForgeLocalAppOwnershipTicket(uint32_t appId);

// Resolves ownership ticket: prefers CredentialStore, falls back to AppID 7 forgery
bool GetAppOwnershipTicket(uint32_t appId, AppOwnershipTicket& outTicket);

using CacheTicketProviderFn = std::vector<uint8_t> (*)(uint32_t appId);
void SetCacheTicketProvider(CacheTicketProviderFn fn);

} // namespace AppTicket
