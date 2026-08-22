#include "LuaConfig.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
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
std::unordered_map<uint32_t, std::vector<std::string>> g_injectModules;
static int Lua_AddAppId(lua_State* L) {
    int n = lua_gettop(L);
    if (n < 1)
        return 0;

    uint32_t appId = static_cast<uint32_t>(lua_tointeger(L, 1));
    std::lock_guard<std::mutex> lock(g_luaMutex);
    g_unlockedApps.insert(appId);

    std::string keyHex;
    if (n >= 3 && lua_isstring(L, 3)) {
        keyHex = lua_tostring(L, 3);
    } else if (n >= 2 && lua_isstring(L, 2) && !lua_isnumber(L, 2)) {
        keyHex = lua_tostring(L, 2);
    }

    if (!keyHex.empty()) {
        auto bytes = OmniPlatform::Encoding::HexToBytes(keyHex);
        g_depotKeys[appId] = bytes;
        spdlog::info("Lua: addappid {} with depotKey ({} bytes)", appId, bytes.size());
    } else {
        spdlog::info("Lua: addappid {}", appId);
    }
    return 0;
}

static int Lua_AddToken(lua_State* L) {
    if (lua_gettop(L) < 2)
        return 0;
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
    if (lua_gettop(L) < 2)
        return 0;
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
    if (lua_gettop(L) < 2)
        return 0;
    uint32_t appId = static_cast<uint32_t>(lua_tointeger(L, 1));
    const char* hex = lua_tostring(L, 2);
    if (hex) {
        OmniPlatform::CredentialStore::WriteTicket(appId, "AppTicket", hex);
        spdlog::info("Lua: setAppTicket for app {}", appId);
    }
    return 0;
}

static int Lua_SetETicket(lua_State* L) {
    if (lua_gettop(L) < 2)
        return 0;
    uint32_t appId = static_cast<uint32_t>(lua_tointeger(L, 1));
    const char* hex = lua_tostring(L, 2);
    if (hex) {
        OmniPlatform::CredentialStore::WriteTicket(appId, "ETicket", hex);
        spdlog::info("Lua: setETicket for app {}", appId);
    }
    return 0;
}

static int Lua_AddInject(lua_State* L) {
    int n = lua_gettop(L);
    if (n < 1)
        return 0;
    uint32_t appId = 0;
    const char* path = nullptr;
    if (n >= 2 && lua_isnumber(L, 1) && lua_isstring(L, 2)) {
        appId = static_cast<uint32_t>(lua_tointeger(L, 1));
        path = lua_tostring(L, 2);
    } else if (n == 1 && lua_isstring(L, 1)) {
        path = lua_tostring(L, 1);
    }
    if (path) {
        std::lock_guard<std::mutex> lock(g_luaMutex);
        g_injectModules[appId].emplace_back(path);
        spdlog::info("Lua: addinject for app {} = {}", appId, path);
    }
    return 0;
}
} // namespace

namespace LuaConfig {

void ParseFile(const std::string& filePath) {
    lua_State* L = luaL_newstate();
    if (!L)
        return;
    luaL_openlibs(L);

    // Sanitize execution environment: neutralize dangerous OS and IO primitives
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");

    lua_getglobal(L, "os");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "execute");
        lua_pushnil(L);
        lua_setfield(L, -2, "remove");
        lua_pushnil(L);
        lua_setfield(L, -2, "rename");
        lua_pushnil(L);
        lua_setfield(L, -2, "exit");
    }
    lua_pop(L, 1);

    lua_getglobal(L, "io");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "popen");
    }
    lua_pop(L, 1);

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
    lua_register(L, "addinject", Lua_AddInject);
    lua_register(L, "addInject", Lua_AddInject);
    lua_register(L, "inject", Lua_AddInject);
    if (luaL_dofile(L, filePath.c_str()) != LUA_OK) {
        spdlog::error("Lua parse error {}: {}", filePath, lua_tostring(L, -1));
    }

    lua_close(L);
}

void ParseDirectory(const std::string& dirPath) {
    if (!fs::exists(dirPath)) {
        return;
    }
    try {
        for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                spdlog::info("Loading Lua file: {}", entry.path().string());
                ParseFile(entry.path().string());
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("LuaConfig: Directory recursion error: {}", e.what());
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
std::unordered_map<uint32_t, std::vector<uint8_t>> GetDepotKeys() {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    return g_depotKeys;
}

std::string GetManifestId(uint32_t depotId) {
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

std::vector<std::string> GetInjectModules(uint32_t appId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    auto it = g_injectModules.find(appId);
    if (it != g_injectModules.end()) {
        return it->second;
    }
    auto itGlobal = g_injectModules.find(0);
    if (itGlobal != g_injectModules.end()) {
        return itGlobal->second;
    }
    return {};
}

} // namespace LuaConfig
