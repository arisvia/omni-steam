#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Config {
void Load(const std::string& configPath);
std::vector<std::string> GetLuaPaths();
std::string GetManifestApiUrl();
bool IsStatsApiEnabled();
bool IsAutoUnlockDlcEnabled();
} // namespace Config
