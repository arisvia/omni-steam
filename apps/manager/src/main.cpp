#include "CoreInstaller.h"
#include "DenuvoImporter.h"
#include "DepotKeyStore.h"
#include "ScriptManager.h"
#include "SteamApi.h"
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
        if (cmd == "install-core") {
            std::string channel = (argc > 2 && std::string(argv[2]) == "--nightly") ? "nightly" : "release";
            std::cout << "[OmniSteam] Installing Core (" << channel << ") to Steam directory...\n";
            bool ok = Manager::CoreInstaller::InstallCore(channel);
            std::cout << (ok ? " [SUCCESS] Core installed successfully!\n" : " [FAILED] Core installation failed.\n");
            return ok ? 0 : 1;
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
