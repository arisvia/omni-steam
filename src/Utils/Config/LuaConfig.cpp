#include "LuaConfig.h"
#include "OmniPlatform/OmniPlatform.h"
#include <lua.hpp>
#include <filesystem>
#include <mutex>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace {
    std::mutex g_luaMutex;
    std::unordered_set<uint32_t> g_unlockedApps;
    std::unordered_map<uint32_t, std::vector<uint8_t>> g_depotKeys;
    std::unordered_map<uint32_t, std::string> g_manifestIds;
    std::unordered_map<uint32_t, std::string> g_accessTokens;

    static int Lua_AddAppId(lua_State* L) {
        int n = lua_gettop(L);
        if (n < 1) return 0;

        uint32_t appId = static_cast<uint32_t>(lua_tointeger(L, 1));
        std::lock_guard<std::mutex> lock(g_luaMutex);
        g_unlockedApps.insert(appId);

        if (n >= 3 && lua_isstring(L, 3)) {
            uint32_t depotId = (n >= 2 && lua_isinteger(L, 2)) ? static_cast<uint32_t>(lua_tointeger(L, 2)) : appId;
            std::string keyHex = lua_tostring(L, 3);
            auto bytes = OmniPlatform::Encoding::HexToBytes(keyHex);
            g_depotKeys[depotId] = bytes;
            spdlog::info("Lua: addappid {} with depotKey for depot {}", appId, depotId);
        } else {
            spdlog::info("Lua: addappid {}", appId);
        }
        return 0;
    }

    static int Lua_AddToken(lua_State* L) {
        if (lua_gettop(L) < 2) return 0;
        uint32_t appId = static_cast<uint32_t>(lua_tointeger(L, 1));
        const char* token = lua_tostring(L, 2);
        if (token) {
            std::lock_guard<std::mutex> lock(g_luaMutex);
            g_accessTokens[appId] = token;
            spdlog::info("Lua: addtoken for app {} = {}", appId, token);
        }
        return 0;
    }

    static int Lua_SetManifestId(lua_State* L) {
        if (lua_gettop(L) < 2) return 0;
        uint32_t depotId = static_cast<uint32_t>(lua_tointeger(L, 1));
        const char* manifestId = lua_tostring(L, 2);
        if (manifestId) {
            std::lock_guard<std::mutex> lock(g_luaMutex);
            g_manifestIds[depotId] = manifestId;
            spdlog::info("Lua: setManifestid for depot {} = {}", depotId, manifestId);
        }
        return 0;
    }

    static int Lua_SetAppTicket(lua_State* L) {
        if (lua_gettop(L) < 2) return 0;
        uint32_t appId = static_cast<uint32_t>(lua_tointeger(L, 1));
        const char* hex = lua_tostring(L, 2);
        if (hex) {
            OmniPlatform::CredentialStore::WriteTicket(appId, "AppTicket", hex);
            spdlog::info("Lua: setAppTicket for app {}", appId);
        }
        return 0;
    }

    static int Lua_SetETicket(lua_State* L) {
        if (lua_gettop(L) < 2) return 0;
        uint32_t appId = static_cast<uint32_t>(lua_tointeger(L, 1));
        const char* hex = lua_tostring(L, 2);
        if (hex) {
            OmniPlatform::CredentialStore::WriteTicket(appId, "ETicket", hex);
            spdlog::info("Lua: setETicket for app {}", appId);
        }
        return 0;
    }
}

namespace LuaConfig {

void ParseFile(const std::string& filePath) {
    lua_State* L = luaL_newstate();
    if (!L) return;
    luaL_openlibs(L);

    lua_register(L, "addappid", Lua_AddAppId);
    lua_register(L, "addAppId", Lua_AddAppId);
    lua_register(L, "addtoken", Lua_AddToken);
    lua_register(L, "addToken", Lua_AddToken);
    lua_register(L, "setManifestid", Lua_SetManifestId);
    lua_register(L, "setmanifestid", Lua_SetManifestId);
    lua_register(L, "setAppTicket", Lua_SetAppTicket);
    lua_register(L, "setappticket", Lua_SetAppTicket);
    lua_register(L, "setETicket", Lua_SetETicket);
    lua_register(L, "seteticket", Lua_SetETicket);

    if (luaL_dofile(L, filePath.c_str()) != LUA_OK) {
        spdlog::error("Lua parse error {}: {}", filePath, lua_tostring(L, -1));
    }

    lua_close(L);
}

void ParseDirectory(const std::string& dirPath) {
    if (!fs::exists(dirPath)) {
        fs::create_directories(dirPath);
        return;
    }

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".lua") {
            spdlog::info("Loading Lua file: {}", entry.path().string());
            ParseFile(entry.path().string());
        }
    }
}

bool HasDepot(uint32_t depotId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    return g_depotKeys.find(depotId) != g_depotKeys.end() || g_unlockedApps.find(depotId) != g_unlockedApps.end();
}

bool HasApp(uint32_t appId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    return g_unlockedApps.find(appId) != g_unlockedApps.end();
}

std::vector<uint8_t> GetDecryptionKey(uint32_t depotId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    auto it = g_depotKeys.find(depotId);
    return it != g_depotKeys.end() ? it->second : std::vector<uint8_t>{};
}

std::string GetManifestId(uint32_t depotId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    auto it = g_manifestIds.find(depotId);
    return it != g_manifestIds.end() ? it->second : "";
}

std::string GetAccessToken(uint32_t appId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    auto it = g_accessTokens.find(appId);
    return it != g_accessTokens.end() ? it->second : "";
}

std::unordered_set<uint32_t> GetUnlockedApps() {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    return g_unlockedApps;
}

} // namespace LuaConfig
