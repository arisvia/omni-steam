#pragma once
#include <cstdint>

// Resolves the "donor" SteamID used for achievement/stats spoofing.
//
// Priority: Lua setStatSteamid override > persistent local cache >
// stats.opensteamtool.com/<appid> (plain uint64 body). Governed by the
// [stats] enable_api config switch.
namespace StatsClient {

bool GetDonorSteamId(uint32_t appId, uint64_t* outSteamId);
uint64_t GetCachedSteamId(uint32_t appId);
void StoreSteamId(uint32_t appId, uint64_t steamId);

} // namespace StatsClient
