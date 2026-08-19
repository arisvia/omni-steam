#include <iostream>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"

#include "ScriptManager.h"
#include "SteamApi.h"
#include "WebServer.h"

namespace Log {
void Init();
}

int main(int argc, char* argv[]) {
    Log::Init();
    spdlog::info("Starting OmniSteam Manager standalone application...");

    uint16_t port = 8080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    if (!Manager::WebServer::Start("127.0.0.1", port)) {
        spdlog::error("Failed to bind WebServer on port {}", port);
        return 1;
    }

    std::cout << "\n======================================================\n";
    std::cout << "🎮 OmniSteam Manager Dashboard Ready!\n";
    std::cout << "👉 Open your browser: http://127.0.0.1:" << port << "\n";
    std::cout << "======================================================\n\n";

    // Keep alive until terminate
    while (Manager::WebServer::IsRunning()) {
        OmniPlatform::Thread::Sleep(1000);
    }

    return 0;
}
