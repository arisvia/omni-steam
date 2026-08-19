#include <cstdlib>
#include <filesystem>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Log {

namespace {
#if defined(OMNI_PLATFORM_WINDOWS)
std::string GetSteamPathFromRegistry() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[MAX_PATH];
        DWORD bufSize = sizeof(buf);
        DWORD type = REG_SZ;
        if (RegQueryValueExA(hKey, "SteamPath", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &bufSize) ==
            ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::string(buf);
        }
        RegCloseKey(hKey);
    }
    return "";
}
#endif

std::string ResolveLogFilePath(const std::string& componentName) {
    std::string logFileName = componentName + ".log";
    std::vector<std::string> candidateLogsDirs;

#if defined(OMNI_PLATFORM_WINDOWS)
    std::string regSteam = GetSteamPathFromRegistry();
    if (!regSteam.empty()) {
        candidateLogsDirs.push_back(regSteam + "/logs");
    }
    candidateLogsDirs.push_back("C:/Program Files (x86)/Steam/logs");
    candidateLogsDirs.push_back("C:/Program Files/Steam/logs");

    const char* appData = std::getenv("APPDATA");
    if (appData) {
        candidateLogsDirs.push_back(std::string(appData) + "/OmniSteam/logs");
    }
#elif defined(OMNI_PLATFORM_MACOS)
    const char* home = std::getenv("HOME");
    if (home) {
        candidateLogsDirs.push_back(std::string(home) + "/Library/Application Support/Steam/logs");
        candidateLogsDirs.push_back(std::string(home) + "/Library/Application Support/OmniSteam/logs");
    }
#else
    const char* home = std::getenv("HOME");
    if (home) {
        candidateLogsDirs.push_back(std::string(home) + "/.local/share/Steam/logs");
        candidateLogsDirs.push_back(std::string(home) + "/.steam/steam/logs");
        candidateLogsDirs.push_back(std::string(home) + "/.config/omnisteam/logs");
    }
#endif

    // Fallback local ./logs
    candidateLogsDirs.push_back("logs");

    for (const auto& dir : candidateLogsDirs) {
        try {
            if (fs::exists(dir)) {
                return (fs::path(dir) / logFileName).generic_string();
            }
            if (fs::create_directories(dir)) {
                return (fs::path(dir) / logFileName).generic_string();
            }
        } catch (...) {
        }
    }

    return logFileName;
}
} // namespace

void Init(const std::string& componentName = "omnisteam_core") {
#if defined(OMNI_PLATFORM_WINDOWS)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    try {
        std::string logPath = ResolveLogFilePath(componentName);
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath, true);
        auto logger = std::make_shared<spdlog::logger>(componentName, spdlog::sinks_init_list{console_sink, file_sink});
        logger->set_level(spdlog::level::debug);
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::debug);
        spdlog::info("OmniSteam Logger initialized -> {}", logPath);
    } catch (...) {
    }
}

} // namespace Log
