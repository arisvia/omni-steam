#include <memory>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>

#include "OmniPlatform/OmniPlatform.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
#endif

namespace Log {

void Init(const std::string& componentName = "omnisteam_core") {
#if defined(OMNI_PLATFORM_WINDOWS)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    try {
        std::string logPath = OmniPlatform::Paths::ResolveLogFilePath(componentName);
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
