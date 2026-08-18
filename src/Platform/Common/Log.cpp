#include "OmniPlatform/OmniPlatform.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace Log {
    void Init() {
        try {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("omnisteam.log", true);
            auto logger = std::make_shared<spdlog::logger>("omnisteam", spdlog::sinks_init_list{console_sink, file_sink});
            logger->set_level(spdlog::level::debug);
            spdlog::set_default_logger(logger);
            spdlog::flush_on(spdlog::level::debug);
            spdlog::info("OmniSteam Logger initialized");
        } catch (...) {}
    }
}
