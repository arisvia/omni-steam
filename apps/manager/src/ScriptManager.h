#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Manager {

struct UnlockGameSpec {
    uint32_t appId = 0;
    std::string gameName;
    std::vector<uint32_t> dlcAppIds;
    std::string accessToken;
    std::string customManifestId;
    std::string depotKeyHex;
};

struct ScriptFileInfo {
    std::string fileName;
    std::string fullPath;
    uint32_t primaryAppId = 0;
    std::string title;
    size_t fileSize = 0;
    bool enabled = true;
};

class ScriptManager {
public:
    static std::string GetDefaultLuaDirectory();
    static std::string GenerateLuaScript(const UnlockGameSpec& spec);
    static bool SaveGameUnlock(const UnlockGameSpec& spec, const std::string& targetDir = "");
    static std::vector<ScriptFileInfo> ListScripts(const std::string& targetDir = "");
    static bool ToggleScript(const std::string& filePath, bool enable);
    static bool DeleteScript(const std::string& filePath);
};

} // namespace Manager
