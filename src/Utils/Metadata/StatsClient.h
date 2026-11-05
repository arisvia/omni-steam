#pragma once
#include <cstdint>

// Resolves the "donor" SteamID used for achievement/stats spoofing.
//
// Priority: Lua setStatSteamid override > persistent local cache >
// stats.opensteamtool.com/<appid> (plain uint64 body). Governed by the
// [stats] enable_api config switch.
namespace StatsClient {

// Cache-only lookup (Lua override, then persisted cache). Never touches the
// network, so it is safe to call from Steam's packet-send thread.
uint64_t TryGetDonorSteamId(uint32_t appId);

// Blocking lookup: TryGetDonorSteamId, then a network fetch. Off the hot path
// only - prefer PrefetchDonorSteamId from hooks.
bool GetDonorSteamId(uint32_t appId, uint64_t* outSteamId);

// Warms the cache on a detached thread so a later TryGetDonorSteamId hits.
// Deduplicated per app while a fetch is in flight.
void PrefetchDonorSteamId(uint32_t appId);

uint64_t GetCachedSteamId(uint32_t appId);
void StoreSteamId(uint32_t appId, uint64_t steamId);

} // namespace StatsClient
