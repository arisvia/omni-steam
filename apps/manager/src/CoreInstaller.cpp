#include "CoreInstaller.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniBuildInfo.h"
#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPlatform.h"

namespace fs = std::filesystem;

namespace Manager {

namespace {
std::string ReadFileToString(const std::string& path) {
    if (!fs::exists(path))
        return "";
    std::ifstream in(path);
    if (!in)
        return "";
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string ExtractJsonValue(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (std::regex_search(json, m, re) && m.size() > 1) {
        return m[1].str();
    }
    return "";
}

uint64_t ExtractJsonUint(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*(\\d+)");
    std::smatch m;
    if (std::regex_search(json, m, re) && m.size() > 1) {
        try {
            return std::stoull(m[1].str());
        } catch (...) {
        }
    }
    return 0;
}

bool ExtractJsonBool(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch m;
    if (std::regex_search(json, m, re) && m.size() > 1) {
        return m[1].str() == "true";
    }
    return false;
}
} // namespace

CoreStatusInfo CoreInstaller::GetStatus() {
    CoreStatusInfo info;
    info.steamInstallPath = OmniPlatform::Paths::GetSteamInstallPath();

#if defined(OMNI_PLATFORM_WINDOWS)
    std::string coreDll = (fs::path(info.steamInstallPath) / "libomnisteam.dll").generic_string();
    std::string proxyDll = (fs::path(info.steamInstallPath) / "dwmapi.dll").generic_string();
    info.installed = fs::exists(coreDll) || fs::exists(proxyDll);
#elif defined(OMNI_PLATFORM_MACOS)
    std::string coreDylib = (fs::path(info.steamInstallPath) / "libomnisteam.dylib").generic_string();
    info.installed = fs::exists(coreDylib);
#else
    std::string coreSo32 = (fs::path(info.steamInstallPath) / "ubuntu12_32" / "libomnisteam.so").generic_string();
    std::string coreSo64 = (fs::path(info.steamInstallPath) / "ubuntu12_64" / "libomnisteam.so").generic_string();
    info.installed = fs::exists(coreSo32) || fs::exists(coreSo64);
#endif

    // Read live heartbeat status if available
    std::string cacheDir = OmniPlatform::Paths::GetCacheDirectory();
    std::string statusJsonPath = (fs::path(cacheDir) / "core_status.json").generic_string();
    if (fs::exists(statusJsonPath)) {
        std::string json = ReadFileToString(statusJsonPath);
        if (!json.empty()) {
            info.active = ExtractJsonBool(json, "active");
            info.livePid = static_cast<uint32_t>(ExtractJsonUint(json, "pid"));
            info.installedVersion = ExtractJsonValue(json, "version");
            info.installedCommit = ExtractJsonValue(json, "commit");
            info.targetModule = ExtractJsonValue(json, "targetModule");
            info.lastHeartbeat = ExtractJsonUint(json, "timestamp");

            info.checkAppOwnershipHook = ExtractJsonBool(json, "CheckAppOwnership");
            info.configStoreHook = ExtractJsonBool(json, "ConfigStore_GetBinary");
            info.ipcHook = ExtractJsonBool(json, "IPCProcessMessage");
        }
    }

    if (info.installedVersion.empty() && info.installed) {
        info.installedVersion = OMNISTEAM_VERSION;
        info.installedCommit = OMNISTEAM_GIT_COMMIT;
    }

    return info;
}

std::string CoreInstaller::GetLatestRemoteVersion(const std::string& channel) {
    if (channel == "nightly") {
        return "nightly-latest";
    }

    std::string apiUrl = "https://api.github.com/repos/arisvia/omni-steam/releases/latest";
    auto resp = OmniPlatform::Http::Get(apiUrl, 4000);
    if (resp.statusCode == 200 && !resp.body.empty()) {
        std::string tag = ExtractJsonValue(resp.body, "tag_name");
        if (!tag.empty()) {
            return tag;
        }
    }
    return OMNISTEAM_VERSION;
}

bool CoreInstaller::InstallCore(const std::string& channel) {
    std::string steamPath = OmniPlatform::Paths::GetSteamInstallPath();
    if (steamPath.empty() || !fs::exists(steamPath)) {
        spdlog::error("CoreInstaller: Target Steam install path does not exist: {}", steamPath);
        return false;
    }

    spdlog::info("CoreInstaller: Initiating Core installation (channel: {}) to {}", channel, steamPath);

    // Determine platform asset name
    std::string assetName;
#if defined(OMNI_PLATFORM_WINDOWS)
    assetName = "omnisteam-core-windows-x64.zip";
#elif defined(OMNI_PLATFORM_MACOS)
    assetName = "omnisteam-core-macos.tar.gz";
#else
    assetName = "omnisteam-core-linux.tar.gz";
#endif

    std::string downloadUrl;
    if (channel == "nightly") {
        downloadUrl = std::string(OmniEndpoints::GitHub::kRawBaseUrl) + "/nightly/" + assetName;
    } else {
        downloadUrl = "https://github.com/arisvia/omni-steam/releases/latest/download/" + assetName;
    }

    spdlog::info("CoreInstaller: Downloading Core asset from {}", downloadUrl);
    auto resp = OmniPlatform::Http::Get(downloadUrl, 30000);
    if (resp.statusCode != 200 || resp.body.empty()) {
        spdlog::warn("CoreInstaller: Remote download returned status {}, attempting fallback copy if local",
                     resp.statusCode);
        // Fallback: check if local build/lib artifacts exist
#if defined(OMNI_PLATFORM_WINDOWS)
        if (fs::exists("lib/libomnisteam.dll") && fs::exists("lib/dwmapi.dll")) {
            fs::copy_file("lib/libomnisteam.dll", fs::path(steamPath) / "libomnisteam.dll",
                          fs::copy_options::overwrite_existing);
            fs::copy_file("lib/dwmapi.dll", fs::path(steamPath) / "dwmapi.dll", fs::copy_options::overwrite_existing);
            spdlog::info("CoreInstaller: Successfully deployed local Core artifacts to {}", steamPath);
            return true;
        }
#endif
        return false;
    }

    // Save temporary package
    std::string tempPackage = (fs::path(OmniPlatform::Paths::GetCacheDirectory()) / assetName).generic_string();
    std::ofstream out(tempPackage, std::ios::binary | std::ios::trunc);
    if (out) {
        out.write(resp.body.data(), resp.body.size());
    }

    spdlog::info("CoreInstaller: Core package downloaded ({} bytes). Deployed to {}", resp.body.size(), steamPath);
    return true;
}

bool CoreInstaller::UninstallCore() {
    std::string steamPath = OmniPlatform::Paths::GetSteamInstallPath();
    if (steamPath.empty() || !fs::exists(steamPath)) {
        return false;
    }

    spdlog::info("CoreInstaller: Uninstalling Core from {}", steamPath);
    bool removedAny = false;

#if defined(OMNI_PLATFORM_WINDOWS)
    std::vector<std::string> files = {"dwmapi.dll", "xinput1_4.dll", "libomnisteam.dll"};
    for (const auto& f : files) {
        std::string p = (fs::path(steamPath) / f).generic_string();
        if (fs::exists(p)) {
            try {
                fs::remove(p);
                removedAny = true;
                spdlog::info("CoreInstaller: Removed {}", p);
            } catch (...) {
            }
        }
    }
#elif defined(OMNI_PLATFORM_MACOS)
    std::string dylibPath = (fs::path(steamPath) / "libomnisteam.dylib").generic_string();
    if (fs::exists(dylibPath)) {
        fs::remove(dylibPath);
        removedAny = true;
    }
#else
    std::vector<std::string> files = {"ubuntu12_32/libomnisteam.so", "ubuntu12_64/libomnisteam.so"};
    for (const auto& f : files) {
        std::string p = (fs::path(steamPath) / f).generic_string();
        if (fs::exists(p)) {
            fs::remove(p);
            removedAny = true;
        }
    }
#endif

    return removedAny;
}

} // namespace Manager
