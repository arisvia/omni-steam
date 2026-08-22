#include "AntiCheatGuard.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/SteamTypes.h"

namespace Security {

namespace {
std::mutex g_antiCheatMutex;
std::unordered_set<uint32_t> g_protectedApps;
bool g_initialized = false;

void RegisterDefaultProtectedApps() {
    using namespace AntiCheat;
    g_protectedApps.insert({kCounterStrike2,
                            kCounterStrikeSource,
                            kDota2,
                            kTeamFortress2,
                            kApexLegends,
                            kPUBG,
                            kRust,
                            kRainbowSixSiege,
                            kDeadByDaylight,
                            kWarframe,
                            kTheFinals,
                            kCallOfDutyHQ,
                            kCallOfDutyWarzone,
                            kNarakaBladepoint,
                            kDestiny2,
                            kHuntShowdown,
                            kEldenRing,
                            kArmoredCoreVI,
                            kHelldivers2,
                            kPalworld,
                            kDayZ,
                            kSquad,
                            kWarThunder,
                            kUnturned,
                            kPayday3,
                            kFC24,
                            kFC25,
                            kSmite,
                            kBrawlhalla,
                            kLeft4Dead2});
}
} // namespace

void AntiCheatGuard::Initialize() {
    std::lock_guard<std::mutex> lock(g_antiCheatMutex);
    if (g_initialized)
        return;
    g_initialized = true;

    RegisterDefaultProtectedApps();
    spdlog::info("AntiCheatGuard: Initialized with {} protected competitive games in stealth whitelist",
                 g_protectedApps.size());
}

bool AntiCheatGuard::IsProtectedApp(uint32_t appId) {
    if (appId == 0)
        return false;

    std::lock_guard<std::mutex> lock(g_antiCheatMutex);
    return g_protectedApps.contains(appId);
}

void AntiCheatGuard::AddProtectedApp(uint32_t appId) {
    if (appId == 0)
        return;

    std::lock_guard<std::mutex> lock(g_antiCheatMutex);
    g_protectedApps.insert(appId);
    spdlog::info("AntiCheatGuard: Added AppID {} to protected stealth whitelist", appId);
}

void AntiCheatGuard::RemoveProtectedApp(uint32_t appId) {
    if (appId == 0)
        return;

    std::lock_guard<std::mutex> lock(g_antiCheatMutex);
    g_protectedApps.erase(appId);
    spdlog::info("AntiCheatGuard: Removed AppID {} from protected stealth whitelist", appId);
}

size_t AntiCheatGuard::Count() {
    std::lock_guard<std::mutex> lock(g_antiCheatMutex);
    return g_protectedApps.size();
}

} // namespace Security
