#include "ScriptManager.h"
#include "SteamApi.h"
#include "WebServer.h"

#include <iostream>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"

namespace Log {
void Init(const std::string& componentName = "omnisteam_core");
}

int main(int argc, char* argv[]) {
    Log::Init("omnisteam_manager");
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
    std::cout << " [OmniSteam] Manager Dashboard Ready!\n";
    std::cout << " -> Open your browser: http://127.0.0.1:" << port << "\n";
    std::cout << "======================================================\n\n";

    // Keep alive until terminate
    while (Manager::WebServer::IsRunning()) {
        OmniPlatform::Thread::Sleep(1000);
    }

    return 0;
}
