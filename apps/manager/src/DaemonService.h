#pragma once

#include <cstdint>

namespace Manager {

class DaemonService {
public:
    // Single responsibility: run WebServer + poll and consume <cache>/events/game_exit/*.json
    static int RunDaemon(uint16_t webPort = 8080);
    static void RefreshAutostart();
};

} // namespace Manager
