#pragma once
#include <string>
#include <cstdint>

namespace Manager {

class WebServer {
public:
    static bool Start(const std::string& host = "127.0.0.1", uint16_t port = 8080);
    static void Stop();
    static bool IsRunning();
};

} // namespace Manager
