#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace EticketClient {

struct FreshTicketResult {
    std::vector<uint8_t> eticket;
    std::vector<uint8_t> appticket;
    uint64_t steamId = 0;
};

// Returns configured backend URL (from Lua seteticketurl or compile-time default)
std::string GetEticketUrl();

// Attempts to mint a fresh on-demand Denuvo ticket bound to the launch nonce.
// Returns nullopt if no backend is configured or if the request fails (falling back to static tickets).
std::optional<FreshTicketResult> FetchFreshEticket(uint32_t appId, std::span<const uint8_t> nonce,
                                                   uint64_t existingSteamId = 0);

// Clears in-memory session cache
void ClearCache();

} // namespace EticketClient
