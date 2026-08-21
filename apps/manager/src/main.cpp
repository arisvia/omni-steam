#include "CloudSaveManager.h"
#include "ConfigManager.h"
#include "CoreInstaller.h"
#include "DenuvoImporter.h"
#include "DepotKeyStore.h"
#include "Doctor.h"
#include "ScriptManager.h"
#include "SteamApi.h"
#include "WebDavClient.h"
#include "WebServer.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>
#include <string>

#include "OmniPlatform/OmniBuildInfo.h"
#include "OmniPlatform/OmniPlatform.h"

namespace Log {
void Init(const std::string& componentName = "omnisteam_core");
}

int main(int argc, char* argv[]) {
    Log::Init("omnisteam_manager");

    // Initialize depot keys in background
    Manager::DepotKeyStore::Initialize();

    if (argc > 1) {
        std::string cmd = argv[1];

        // 1. Help & Usage
        if (cmd == "--help" || cmd == "-h" || cmd == "help") {
            std::cout << "\n======================================================\n";
            std::cout << " OmniSteam Manager CLI (v" << OMNISTEAM_VERSION << ")\n";
            std::cout << "======================================================\n";
            std::cout << "Usage: omnisteam [command] [options]\n\n";
            std::cout << "Commands:\n";
            std::cout << "  (no args)             Start Web Dashboard (default port 8080)\n";
            std::cout << "  [port]                Start Web Dashboard on specified port\n";
            std::cout << "  doctor                Run comprehensive system health check\n";
            std::cout << "  core-status           Inspect Steam hook attachment and PID\n";
            std::cout << "  install-core [--nightly]  Install hook DLL/SO to Steam folder\n";
            std::cout << "  uninstall-core        Remove hook DLL/SO from Steam folder\n";
            std::cout << "  search <query>        Search Steam Store games and get AppIDs\n";
            std::cout << "  unlock <appid>        Unlock game & all DLCs, resolve keys & generate Lua\n";
            std::cout << "  list                  List all unlocked games and script status\n";
            std::cout << "  toggle <appid> [0|1]  Enable (1) or Disable (0) an unlocked game\n";
            std::cout << "  remove <appid>        Remove an unlocked game Lua script\n";
            std::cout << "  import-ticket <file>  Import Denuvo binary ticket (.bin / .txt)\n";
            std::cout << "  backup [appid]        Backup game saves to configured WebDAV\n";
            std::cout << "  restore <appid>       Restore game saves from WebDAV\n";
            std::cout << "======================================================\n\n";
            return 0;
        }

        // 2. Doctor Diagnostics
        if (cmd == "doctor" || cmd == "diagnose" || cmd == "check") {
            auto report = Manager::Doctor::RunDiagnostics();
            Manager::Doctor::PrintReport(report);
            return report.overallHealthy ? 0 : 1;
        }

        // 3. Core Management
        if (cmd == "install-core") {
            std::string channel = (argc > 2 && std::string(argv[2]) == "--nightly") ? "nightly" : "release";
            std::cout << "[OmniSteam] Installing Core (" << channel << ") to Steam directory...\n";
            auto res = Manager::CoreInstaller::InstallCore(channel);
            std::cout << (res.success ? " [SUCCESS] " : " [FAILED] ") << res.message << "\n";
            return res.success ? 0 : 1;
        }

        if (cmd == "uninstall-core") {
            std::cout << "[OmniSteam] Uninstalling Core from Steam directory...\n";
            bool ok = Manager::CoreInstaller::UninstallCore();
            std::cout << (ok ? " [SUCCESS] Core uninstalled.\n" : " [NOTICE] No Core files found to remove.\n");
            return 0;
        }

        if (cmd == "core-status") {
            auto s = Manager::CoreInstaller::GetStatus();
            std::cout << "\n======================================================\n";
            std::cout << " OmniSteam Core Status (CLI v" << OMNISTEAM_VERSION << ")\n";
            std::cout << "======================================================\n";
            std::cout << " Steam Path:  " << s.steamInstallPath << "\n";
            std::cout << " Installed:   " << (s.installed ? "YES" : "NO") << "\n";
            std::cout << " Live Hook:   " << (s.active ? "ACTIVE (Hooked)" : "INACTIVE / NOT RUNNING") << "\n";
            if (s.active) {
                std::cout << " Steam PID:   " << s.livePid << "\n";
                std::cout << " Target Mod:  " << s.targetModule << "\n";
            }
            std::cout << "======================================================\n\n";
            return 0;
        }

        // 4. Search Steam Store
        if (cmd == "search" && argc > 2) {
            std::string query = argv[2];
            std::cout << "[OmniSteam] Searching Steam Store for '" << query << "'...\n";
            auto results = Manager::SteamApi::SearchStore(query);
            if (results.empty()) {
                std::cout << " [NOTICE] No matching games found.\n";
                return 0;
            }
            std::cout << "\nFound " << results.size() << " results:\n";
            for (const auto& item : results) {
                std::cout << "  AppID: " << item.appId << "\t| " << item.name << "\n";
            }
            std::cout << "\nTo unlock, run: omnisteam unlock <AppID>\n\n";
            return 0;
        }

        // 5. Unlock Game directly
        if (cmd == "unlock" && argc > 2) {
            uint32_t appId = 0;
            try {
                appId = static_cast<uint32_t>(std::stoul(argv[2]));
            } catch (...) {
                std::cerr << " [ERROR] Invalid AppID: " << argv[2] << "\n";
                return 1;
            }

            std::cout << "[OmniSteam] Querying metadata and DLCs for AppID " << appId << "...\n";
            auto details = Manager::SteamApi::GetAppDetails(appId);
            Manager::UnlockGameSpec spec;
            spec.appId = appId;
            spec.gameName = details.isSuccess ? details.name : ("App " + std::to_string(appId));
            if (details.isSuccess) {
                spec.dlcAppIds = details.dlcAppIds;
            }

            // Auto-resolve depot keys from local DB
            auto keys = Manager::DepotKeyStore::FindDepotKeysForApp(appId, spec.dlcAppIds);
            for (const auto& kv : keys) {
                spec.depotKeys[kv.first] = kv.second;
            }
            bool saved = Manager::ScriptManager::SaveGameUnlock(spec);
            if (saved) {
                std::cout << " [SUCCESS] Unlocked " << spec.gameName << " (AppID: " << appId << ")\n";
                std::cout << "           - Included DLCs: " << spec.dlcAppIds.size() << "\n";
                std::cout << "           - Depot Keys:    " << spec.depotKeys.size() << " loaded\n";
                std::cout << "           - Hot-Reload:    Dispatched to Steam\n\n";
                return 0;
            } else {
                std::cerr << " [FAILED] Could not write Lua script for AppID " << appId << "\n";
                return 1;
            }
        }

        // 6. List Unlocked Games
        if (cmd == "list" || cmd == "list-unlocked" || cmd == "scripts") {
            auto scripts = Manager::ScriptManager::ListScripts();
            std::cout << "\n======================================================\n";
            std::cout << " OmniSteam Unlocked Games (" << scripts.size() << " total)\n";
            std::cout << "======================================================\n";
            if (scripts.empty()) {
                std::cout << " (No unlocked games found in config/lua directory)\n";
            } else {
                for (const auto& s : scripts) {
                    std::cout << " [" << (s.enabled ? "ACTIVE" : "DISABLED") << "]\t"
                              << "AppID: " << s.primaryAppId << "\t"
                              << "| " << s.title << " (" << s.fileName << ")\n";
                }
            }
            std::cout << "======================================================\n\n";
            return 0;
        }

        // 7. Toggle Script
        if (cmd == "toggle" && argc > 2) {
            std::string target = argv[2];
            bool enableState = true;
            if (argc > 3) {
                std::string st = argv[3];
                enableState = (st == "1" || st == "true" || st == "on" || st == "enable");
            }

            auto scripts = Manager::ScriptManager::ListScripts();
            bool found = false;
            for (const auto& s : scripts) {
                if (std::to_string(s.primaryAppId) == target || s.fileName == target ||
                    s.fileName == (target + ".lua") || s.fileName == (target + ".lua.bak")) {
                    bool ok = Manager::ScriptManager::ToggleScript(s.fullPath, enableState);
                    std::cout << (ok ? " [SUCCESS] " : " [FAILED] ") << (enableState ? "Enabled " : "Disabled ")
                              << s.title << "\n";
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cerr << " [ERROR] Could not find script matching: " << target << "\n";
                return 1;
            }
            return 0;
        }

        // 8. Remove Script
        if ((cmd == "remove" || cmd == "delete" || cmd == "rm") && argc > 2) {
            std::string target = argv[2];
            auto scripts = Manager::ScriptManager::ListScripts();
            bool found = false;
            for (const auto& s : scripts) {
                if (std::to_string(s.primaryAppId) == target || s.fileName == target ||
                    s.fileName == (target + ".lua") || s.fileName == (target + ".lua.bak")) {
                    bool ok = Manager::ScriptManager::DeleteScript(s.fullPath);
                    std::cout << (ok ? " [SUCCESS] " : " [FAILED] ") << "Removed " << s.title << " (" << s.fileName
                              << ")\n";
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cerr << " [ERROR] Could not find script matching: " << target << "\n";
                return 1;
            }
            return 0;
        }

        // 9. Denuvo Ticket Import
        if (cmd == "import-ticket" && argc > 2) {
            std::string ticketPath = argv[2];
            std::cout << "[OmniSteam] Importing Denuvo ticket from " << ticketPath << "...\n";
            std::ifstream in(ticketPath, std::ios::binary);
            if (!in) {
                std::cerr << " [ERROR] Could not open file: " << ticketPath << "\n";
                return 1;
            }
            std::string payload((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            auto res = Manager::DenuvoImporter::ImportFromPayload(payload, ticketPath);
            if (res.success) {
                std::cout << " [SUCCESS] " << res.message << "\n";
                std::cout << " Game:        " << res.gameName << " (AppID: " << res.appId << ")\n";
                std::cout << " DLCs:        " << res.dlcCount << " unlocked\n";
                std::cout << " Depot Keys:  " << res.resolvedDepotKeysCount << " loaded\n";
                if (!res.missingDepots.empty()) {
                    std::cout << " [NOTICE] " << res.missingDepots.size() << " depot keys missing in local database.\n";
                }
                return 0;
            } else {
                std::cerr << " [FAILED] " << res.message << "\n";
                return 1;
            }
        }

        // 10. Backup Saves
        if (cmd == "backup") {
            auto config = Manager::ConfigManager::ReadConfig();
            if (!config.cloudEnabled || config.webdavServerUrl.empty()) {
                std::cerr << " [ERROR] Cloud save is not configured or disabled.\n";
                std::cerr << "         Please configure WebDAV via WebUI or config.toml.\n";
                return 1;
            }
            Manager::WebDavConfig webdav;
            webdav.serverUrl = config.webdavServerUrl;
            webdav.username = config.webdavUsername;
            webdav.password = config.webdavPassword;
            webdav.remoteRootPath = config.webdavRemoteRoot.empty() ? "OmniSteam_Saves" : config.webdavRemoteRoot;
            if (argc > 2) {
                uint32_t appId = static_cast<uint32_t>(std::stoul(argv[2]));
                std::cout << "[OmniSteam] Backing up saves for AppID " << appId << " to WebDAV...\n";
                bool ok = Manager::CloudSaveManager::BackupAppSaves(appId, webdav);
                std::cout << (ok ? " [SUCCESS] Backup completed.\n" : " [FAILED] Backup failed.\n");
                return ok ? 0 : 1;
            } else {
                std::cout << "[OmniSteam] Backing up saves for all unlocked games to WebDAV...\n";
                auto scripts = Manager::ScriptManager::ListScripts();
                int okCount = 0;
                for (const auto& s : scripts) {
                    if (s.primaryAppId != 0) {
                        if (Manager::CloudSaveManager::BackupAppSaves(s.primaryAppId, webdav)) {
                            std::cout << "  [OK] " << s.title << " (AppID: " << s.primaryAppId << ")\n";
                            okCount++;
                        }
                    }
                }
                std::cout << "[OmniSteam] Batch backup finished: " << okCount << " succeeded.\n";
                return 0;
            }
        }

        // 11. Restore Saves
        if (cmd == "restore" && argc > 2) {
            uint32_t appId = static_cast<uint32_t>(std::stoul(argv[2]));
            auto config = Manager::ConfigManager::ReadConfig();
            if (!config.cloudEnabled || config.webdavServerUrl.empty()) {
                std::cerr << " [ERROR] Cloud save is not configured or disabled.\n";
                return 1;
            }
            Manager::WebDavConfig webdav;
            webdav.serverUrl = config.webdavServerUrl;
            webdav.username = config.webdavUsername;
            webdav.password = config.webdavPassword;
            webdav.remoteRootPath = config.webdavRemoteRoot.empty() ? "OmniSteam_Saves" : config.webdavRemoteRoot;
            std::cout << "[OmniSteam] Restoring saves for AppID " << appId << " from WebDAV...\n";
            bool ok = Manager::CloudSaveManager::RestoreAppSaves(appId, webdav);
            std::cout << (ok ? " [SUCCESS] Restore completed.\n" : " [FAILED] Restore failed.\n");
            return ok ? 0 : 1;
        }
    }

    spdlog::info("Starting OmniSteam standalone CLI application...");

    uint16_t port = 8080;
    if (argc > 1) {
        try {
            port = static_cast<uint16_t>(std::stoi(argv[1]));
        } catch (...) {
        }
    }

    if (!Manager::WebServer::Start("127.0.0.1", port)) {
        spdlog::error("Failed to bind WebServer on port {}", port);
        return 1;
    }

    std::cout << "\n======================================================\n";
    std::cout << " [OmniSteam] Manager Dashboard Ready! (v" << OMNISTEAM_VERSION << ")\n";
    std::cout << " -> Open your browser: http://127.0.0.1:" << port << "\n";
    std::cout << "======================================================\n\n";

    // Keep alive until terminate
    while (Manager::WebServer::IsRunning()) {
        OmniPlatform::Thread::Sleep(1000);
    }

    return 0;
}
