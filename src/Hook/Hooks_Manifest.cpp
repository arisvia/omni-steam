#include <cstdint>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <string>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/ManifestClient.h"

namespace fs = std::filesystem;

namespace Hooks_Manifest {

bool EnsureDepotManifest(uint32_t depotId, uint64_t manifestId) {
    if (depotId == 0 || manifestId == 0)
        return false;

    std::string steamPath = OmniPlatform::Paths::GetSteamInstallPath();
    if (steamPath.empty())
        return false;

    std::string cacheDir = (fs::path(steamPath) / "depotcache").generic_string();
    std::string manifestFile =
        (fs::path(cacheDir) / (std::to_string(depotId) + "_" + std::to_string(manifestId) + ".manifest"))
            .generic_string();

    if (fs::exists(manifestFile)) {
        return true;
    }

    spdlog::info("Hooks_Manifest: Manifest missing in depotcache, fetching from upstream for depot {} (GID: {})",
                 depotId, manifestId);

    auto result = ManifestClient::RequestManifest(depotId, manifestId);
    if (result.success && !result.payload.empty()) {
        try {
            fs::create_directories(cacheDir);
            std::ofstream out(manifestFile, std::ios::binary | std::ios::trunc);
            if (out) {
                out.write(reinterpret_cast<const char*>(result.payload.data()), result.payload.size());
                spdlog::info("Hooks_Manifest: Successfully saved manifest to {}", manifestFile);
                return true;
            }
        } catch (const std::exception& e) {
            spdlog::warn("Hooks_Manifest: Failed to write manifest file {}: {}", manifestFile, e.what());
        }
    }
    return false;
}

void Install() {
    spdlog::info("Hooks_Manifest: Manifest redirection engine initialized");

    // Scan all unlocked depots for configured custom manifest IDs and pre-cache them
    auto unlockedApps = LuaConfig::GetUnlockedApps();
    for (uint32_t id : unlockedApps) {
        std::string manifestGidStr = LuaConfig::GetManifestId(id);
        if (!manifestGidStr.empty()) {
            try {
                uint64_t manifestGid = std::stoull(manifestGidStr);
                EnsureDepotManifest(id, manifestGid);
            } catch (...) {
            }
        }
    }
}

void Uninstall() {}

} // namespace Hooks_Manifest
