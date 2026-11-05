#include "DaemonService.h"

#include "CloudSaveManager.h"
#include "ConfigManager.h"
#include "WebServer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
#include <vector>

#include "OmniPlatform/OmniPaths.h"
#include "OmniPlatform/OmniPlatform.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Manager {

namespace {

std::atomic<bool> g_daemonRunning{false};

std::string GetEventsDirectory() {
    return (fs::path(OmniPlatform::Paths::GetCacheDirectory()) / "events" / "game_exit").generic_string();
}

void ProcessPendingGameExitEvents() {
    std::string dir = GetEventsDirectory();
    if (!fs::exists(dir)) {
        return;
    }

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        std::string filePath = entry.path().generic_string();
        std::ifstream file(filePath);
        if (!file) {
            continue;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        // Extract "appId": 12345
        static const std::regex appIdRe(R"regex("appId"\s*:\s*(\d+))regex");
        std::smatch m;
        if (std::regex_search(content, m, appIdRe)) {
            try {
                uint32_t appId = static_cast<uint32_t>(std::stoul(m[1].str()));
                spdlog::info("DaemonService: Consuming game exit event for AppID {}", appId);

                // Run save backup gracefully
                auto config = ConfigManager::ReadConfig();
                WebDavConfig webdav;
                if (config.cloudEnabled && !config.webdavServerUrl.empty()) {
                    webdav.serverUrl = config.webdavServerUrl;
                    webdav.username = config.webdavUsername;
                    webdav.password = config.webdavPassword;
                    webdav.remoteRootPath = config.webdavRemoteRoot;
                }
                CloudSaveManager::BackupAppSaves(appId, webdav);
            } catch (const std::exception& e) {
                spdlog::warn("DaemonService: Failed to parse event {}: {}", filePath, e.what());
            }
        }

        // Remove consumed event file
        fs::remove(filePath, ec);
    }
}

void EventPollLoop() {
    while (g_daemonRunning.load()) {
        try {
            ProcessPendingGameExitEvents();
        } catch (...) {
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

} // namespace

void DaemonService::RefreshAutostart() {
    std::string currentExe = OmniPlatform::Process::GetExecutablePath();
    if (currentExe.empty()) {
        return;
    }

#if defined(OMNI_PLATFORM_WINDOWS)
    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE,
                      &hKey) == ERROR_SUCCESS) {
        std::string cmd = "\"" + currentExe + "\" daemon";
        RegSetValueExA(hKey, "OmniSteamDaemon", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       static_cast<DWORD>(cmd.size() + 1));
        RegCloseKey(hKey);
    }
#else
    const char* home = std::getenv("HOME");
    if (!home)
        return;
    std::string autostartDir = std::string(home) + "/.config/autostart";
    std::error_code ec;
    fs::create_directories(autostartDir, ec);
    std::string desktopFile = autostartDir + "/omnisteam-daemon.desktop";
    std::ofstream out(desktopFile);
    if (out) {
        out << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=OmniSteam Daemon\n"
            << "Exec=" << currentExe << " daemon\n"
            << "Hidden=false\n"
            << "X-GNOME-Autostart-enabled=true\n";
    }
#endif
}

int DaemonService::RunDaemon(uint16_t webPort) {
    if (g_daemonRunning.load()) {
        return 0;
    }
    g_daemonRunning.store(true);

    spdlog::info("DaemonService: Starting OmniSteam Daemon on web port {}...", webPort);

    // 1. Start event polling thread to consume <cache>/events/game_exit/*.json
    std::thread pollThread(EventPollLoop);
    pollThread.detach();

    // 2. Start WebServer in current thread (blocking until termination)
    WebServer::Start("127.0.0.1", webPort);
    while (g_daemonRunning.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    spdlog::info("DaemonService: OmniSteam Daemon stopped.");
    return 0;
}

} // namespace Manager
