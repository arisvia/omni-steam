#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LuaConfig {
void ParseDirectory(const std::string& dirPath);
void ReloadDirectories(const std::string& dirPath);
void ParseFile(const std::string& filePath);
bool HasDepot(uint32_t depotId);
bool HasApp(uint32_t appId);
std::vector<uint8_t> GetDecryptionKey(uint32_t depotId);
std::unordered_map<uint32_t, std::vector<uint8_t>> GetDepotKeys();
std::string GetManifestId(uint32_t depotId);
std::string GetAccessToken(uint32_t appId);
std::unordered_set<uint32_t> GetUnlockedApps();
std::vector<uint32_t> GetAllDepotIds();
std::vector<std::string> GetInjectModules(uint32_t appId);
} // namespace LuaConfig
