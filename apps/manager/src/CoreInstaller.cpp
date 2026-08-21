#include "CoreInstaller.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#endif

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

bool IsProcessAlive(uint32_t pid) {
    if (pid == 0)
        return false;
#if defined(OMNI_PLATFORM_WINDOWS)
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
            CloseHandle(hProcess);
            return true;
        }
        CloseHandle(hProcess);
    }
    return false;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0;
#endif
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

    // Invalidate stale status if the target process is no longer alive
    if (info.active && (info.livePid == 0 || !IsProcessAlive(info.livePid))) {
        info.active = false;
        info.livePid = 0;
        info.checkAppOwnershipHook = false;
        info.configStoreHook = false;
        info.ipcHook = false;
        std::error_code ec;
        fs::remove(statusJsonPath, ec);
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

    std::string apiUrl = OmniEndpoints::GitHub::kApiReleasesLatest;
    auto resp = OmniPlatform::Http::Get(apiUrl, 4000);
    if (resp.statusCode == 200 && !resp.body.empty()) {
        std::string tag = ExtractJsonValue(resp.body, "tag_name");
        if (!tag.empty()) {
            return tag;
        }
    }
    return OMNISTEAM_VERSION;
}

InstallResult CoreInstaller::InstallCore(const std::string& channel) {
    std::string steamPath = OmniPlatform::Paths::GetSteamInstallPath();
    if (steamPath.empty() || !fs::exists(steamPath)) {
        spdlog::error("CoreInstaller: Target Steam install path does not exist: {}", steamPath);
        return {false, "未检测到有效的 Steam 安装目录，请确认 Steam 客户端已安装并至少运行过一次。"};
    }

    spdlog::info("CoreInstaller: Initiating Core installation (channel: {}) to {}", channel, steamPath);

    // Determine candidate platform asset names
    std::vector<std::string> assetCandidates;
#if defined(OMNI_PLATFORM_WINDOWS)
    assetCandidates = {"omnisteam-core-windows-x64.zip", "omnisteam-windows-latest-x86_64.zip",
                       "omnisteam-core-windows.zip"};
#elif defined(OMNI_PLATFORM_MACOS)
    assetCandidates = {"omnisteam-core-macos-arm64.tar.gz", "omnisteam-macos-latest-arm64.tar.gz",
                       "omnisteam-core-macos.tar.gz"};
#else
    assetCandidates = {"omnisteam-core-linux-x64.tar.gz", "omnisteam-ubuntu-latest-x86_64.tar.gz",
                       "omnisteam-core-linux.tar.gz"};
#endif

    // 1. Check local sibling binary files first (for developer or manual unpacked builds)
#if defined(OMNI_PLATFORM_WINDOWS)
    std::vector<std::string> localSearchDirs = {
        ".",
        "lib",
        "bin",
        "Release",
        "build/bin/Release",
        "build/bin/Debug",
        "build/lib",
        (fs::path(OmniPlatform::Process::GetExecutablePath()).parent_path()).generic_string()};
    for (const auto& d : localSearchDirs) {
        std::string coreDll = (fs::path(d) / "libomnisteam.dll").generic_string();
        std::string proxyDll = (fs::path(d) / "dwmapi.dll").generic_string();
        if (fs::exists(coreDll) && fs::exists(proxyDll)) {
            fs::copy_file(coreDll, fs::path(steamPath) / "libomnisteam.dll", fs::copy_options::overwrite_existing);
            fs::copy_file(proxyDll, fs::path(steamPath) / "dwmapi.dll", fs::copy_options::overwrite_existing);
            spdlog::info("CoreInstaller: Successfully deployed local Core binaries from {} to {}", d, steamPath);
            return {true, "已成功从本地构建产物 (" + d +
                              ") 部署 Core 拦截引擎 (libomnisteam.dll + dwmapi.dll) 到 Steam 目录！"};
        }
    }
#endif

    // 2. Fetch from GitHub release / nightly with automatic fallback URLs and archive extraction
    for (const auto& assetName : assetCandidates) {
        std::vector<std::string> downloadSources;
        if (channel == "nightly") {
            downloadSources.push_back(std::string(OmniEndpoints::GitHub::kRawBaseUrl) + "/nightly/" + assetName);
            downloadSources.push_back(std::string(OmniEndpoints::GitHub::kJsDelivrNightlyBase) + assetName);
        } else {
            downloadSources.push_back(std::string(OmniEndpoints::GitHub::kReleasesLatestDownload) + assetName);
            downloadSources.push_back(std::string(OmniEndpoints::GitHub::kReleasesDownloadBase) + "v" +
                                      std::string(OMNISTEAM_VERSION) + "/" + assetName);
            downloadSources.push_back(std::string(OmniEndpoints::GitHub::kRawBaseUrl) + "/main/" + assetName);
            downloadSources.push_back(std::string(OmniEndpoints::GitHub::kJsDelivrMainBase) + assetName);
        }
        for (const auto& downloadUrl : downloadSources) {
            spdlog::info("CoreInstaller: Downloading Core asset from {}", downloadUrl);
            auto resp = OmniPlatform::Http::Get(downloadUrl, 30000);
            if (resp.statusCode == 200 && !resp.body.empty()) {
                std::string tempPackage =
                    (fs::path(OmniPlatform::Paths::GetCacheDirectory()) / assetName).generic_string();
                std::ofstream out(tempPackage, std::ios::binary | std::ios::trunc);
                if (out) {
                    out.write(resp.body.data(), resp.body.size());
                }
                spdlog::info("CoreInstaller: Core package downloaded ({} bytes). Saved to {}", resp.body.size(),
                             tempPackage);

                // Automatically unpack archive into target Steam directory
#if defined(OMNI_PLATFORM_WINDOWS)
                std::string unpackCmd =
                    "tar -xf \"" + tempPackage + "\" -C \"" + steamPath +
                    "\" 2>nul || powershell -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -Path '" +
                    tempPackage + "' -DestinationPath '" + steamPath + "' -Force\"";
#else
                std::string unpackCmd = "tar -xzf \"" + tempPackage + "\" -C \"" + steamPath + "\" 2>/dev/null";
#endif
                int unpackResult = std::system(unpackCmd.c_str());
                spdlog::info("CoreInstaller: Archive extraction executed (result: {})", unpackResult);
                return {true, "已成功从远程下载并解压部署 OmniSteam Core 引擎到 Steam 目录！"};
            }
        }
    }

    spdlog::warn("CoreInstaller: All remote asset candidate downloads failed");
    return {false, "未检测到远程每夜版资产（GitHub 仓库暂未发布 Release 产物）。如为本地源码开发，请先在 build/ "
                   "目录编译生成 Core 动态库 (libomnisteam.dll / dwmapi.dll) 后点击安装。"};
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
