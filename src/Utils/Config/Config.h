#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Config {
    void Load(const std::string& configPath);
    std::vector<std::string> GetLuaPaths();
    std::string GetManifestApiUrl();
    bool IsStatsApiEnabled();
}
