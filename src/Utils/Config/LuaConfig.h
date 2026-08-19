#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LuaConfig {
void ParseDirectory(const std::string& dirPath);
void ParseFile(const std::string& filePath);

bool HasDepot(uint32_t depotId);
bool HasApp(uint32_t appId);
std::vector<uint8_t> GetDecryptionKey(uint32_t depotId);
std::string GetManifestId(uint32_t depotId);
std::string GetAccessToken(uint32_t appId);
std::unordered_set<uint32_t> GetUnlockedApps();
} // namespace LuaConfig
