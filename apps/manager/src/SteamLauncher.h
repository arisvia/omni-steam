#pragma once

#include <cstdint>
#include <string>

namespace Manager {

struct LaunchResult {
    bool success = false;
    std::string message;
};

// Launches Steam with the platform injection mechanism active:
// Windows relies on the xinput1_4 proxy already deployed in the Steam root;
// Linux prepends LD_PRELOAD=<steam>/ubuntu12_64/libomnisteam.so;
// macOS prepends DYLD_INSERT_LIBRARIES=<steam>/libomnisteam.dylib and execs
// Steam.app's steam_osx binary directly (open -a does not forward env vars).
// Refuses to launch a second instance while Steam is running.
class SteamLauncher {
public:
    static LaunchResult Launch();
    static bool IsSteamRunning(uint32_t* outPid = nullptr);
};

} // namespace Manager