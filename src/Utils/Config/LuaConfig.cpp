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

// Double-buffered store: readers always see a fully populated snapshot while
// the hot-reload thread rebuilds the inactive slot, then swaps atomically
// under the mutex.
struct LuaData {
    std::unordered_set<uint32_t> unlockedApps;
    std::unordered_map<uint32_t, std::vector<uint8_t>> depotKeys;
    std::unordered_map<uint32_t, std::string> manifestIds;
    std::unordered_map<uint32_t, std::string> accessTokens;
    std::unordered_map<uint32_t, std::vector<std::string>> injectModules;
};

std::mutex g_luaMutex;
std::mutex g_reloadMutex;
LuaData g_slots[2];
size_t g_activeSlot = 0;

LuaData& MutableActive() {
    return g_slots[g_activeSlot];
}

int* RegistryTargetKey() {
    static int key = 0;
    return &key;
}

LuaData* TargetFromRegistry(lua_State* L) {
    lua_pushlightuserdata(L, RegistryTargetKey());
    lua_gettable(L, LUA_REGISTRYINDEX);
    auto* target = static_cast<LuaData*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return target ? target : &MutableActive();
}

static int Lua_AddAppId(lua_State* L) {
    int n = lua_gettop(L);
    if (n < 1)
        return 0;

    uint32_t appId = static_cast<uint32_t>(lua_tointeger(L, 1));
    std::string keyHex;
    if (n >= 3 && lua_isstring(L, 3)) {
        keyHex = lua_tostring(L, 3);
    } else if (n >= 2 && lua_isstring(L, 2) && !lua_isnumber(L, 2)) {
        keyHex = lua_tostring(L, 2);
    }

    auto* target = TargetFromRegistry(L);
    std::lock_guard<std::mutex> lock(g_luaMutex);
    if (!keyHex.empty()) {
        auto bytes = OmniPlatform::Encoding::HexToBytes(keyHex);
        target->depotKeys[appId] = bytes;
        spdlog::info("Lua: addappid {} with depotKey ({} bytes)", appId, bytes.size());
    } else {
        target->unlockedApps.insert(appId);
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
        auto* target = TargetFromRegistry(L);
        std::lock_guard<std::mutex> lock(g_luaMutex);
        target->accessTokens[appId] = token;
        spdlog::info("Lua: addtoken for app {}", appId);
    }
    return 0;
}

static int Lua_SetManifestId(lua_State* L) {
    if (lua_gettop(L) < 2)
        return 0;
    uint32_t depotId = static_cast<uint32_t>(lua_tointeger(L, 1));
    const char* manifestId = lua_tostring(L, 2);
    if (manifestId) {
        auto* target = TargetFromRegistry(L);
        std::lock_guard<std::mutex> lock(g_luaMutex);
        target->manifestIds[depotId] = manifestId;
        spdlog::info("Lua: setManifestid for depot {}", depotId);
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
        auto* target = TargetFromRegistry(L);
        std::lock_guard<std::mutex> lock(g_luaMutex);
        target->injectModules[appId].emplace_back(path);
        spdlog::info("Lua: addinject for app {} = {}", appId, path);
    }
    return 0;
}

void SanitizeLuaEnvironment(lua_State* L) {
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
    lua_pushnil(L);
    lua_setglobal(L, "io");

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
        lua_pushnil(L);
        lua_setfield(L, -2, "setlocale");
    }
    lua_pop(L, 1);

    lua_getglobal(L, "package");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "loadlib");
    }
    lua_pop(L, 1);
}

void RegisterLuaApi(lua_State* L, LuaData* target) {
    lua_pushlightuserdata(L, RegistryTargetKey());
    lua_pushlightuserdata(L, target);
    lua_settable(L, LUA_REGISTRYINDEX);

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
}

void RunLuaFile(const std::string& filePath, LuaData* target) {
    lua_State* L = luaL_newstate();
    if (!L)
        return;
    luaL_openlibs(L);

    SanitizeLuaEnvironment(L);
    RegisterLuaApi(L, target);

    if (luaL_dofile(L, filePath.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        spdlog::error("Lua parse error {}: {}", filePath, err ? err : "(non-string error object)");
    }

    lua_close(L);
}

} // namespace

namespace LuaConfig {

void ParseFile(const std::string& filePath) {
    RunLuaFile(filePath, &MutableActive());
}

void ParseDirectoryInto(const std::string& dirPath, size_t slotIndex) {
    if (!fs::exists(dirPath)) {
        return;
    }
    try {
        for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                spdlog::info("Loading Lua file: {}", entry.path().string());
                RunLuaFile(entry.path().string(), &g_slots[slotIndex]);
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("LuaConfig: Directory recursion error: {}", e.what());
    }
}

void ParseDirectory(const std::string& dirPath) {
    ParseDirectoryInto(dirPath, g_activeSlot);
}

void ReloadDirectories(const std::string& dirPath) {
    ReloadDirectories(std::vector<std::string>{dirPath});
}

void ReloadDirectories(const std::vector<std::string>& dirPaths) {
    std::lock_guard<std::mutex> reloadLock(g_reloadMutex);

    size_t stagingSlot = 0;
    {
        std::lock_guard<std::mutex> lock(g_luaMutex);
        stagingSlot = 1 - g_activeSlot;
        g_slots[stagingSlot] = LuaData{};
    }

    spdlog::info("LuaConfig: Rebuilding configuration snapshot from {} director(y/ies)", dirPaths.size());
    for (const auto& dirPath : dirPaths) {
        ParseDirectoryInto(dirPath, stagingSlot);
    }

    {
        std::lock_guard<std::mutex> lock(g_luaMutex);
        g_activeSlot = stagingSlot;
        size_t staleSlot = 1 - stagingSlot;
        g_slots[staleSlot] = LuaData{};
    }
    spdlog::info("LuaConfig: Configuration snapshot activated");
}

bool HasDepot(uint32_t depotId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    const LuaData& active = g_slots[g_activeSlot];
    return active.depotKeys.find(depotId) != active.depotKeys.end() ||
           active.unlockedApps.find(depotId) != active.unlockedApps.end();
}

bool HasApp(uint32_t appId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    const LuaData& active = g_slots[g_activeSlot];
    return active.unlockedApps.find(appId) != active.unlockedApps.end() ||
           active.depotKeys.find(appId) != active.depotKeys.end();
}
std::vector<uint8_t> GetDecryptionKey(uint32_t depotId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    const LuaData& active = g_slots[g_activeSlot];
    auto it = active.depotKeys.find(depotId);
    return it != active.depotKeys.end() ? it->second : std::vector<uint8_t>{};
}
std::unordered_map<uint32_t, std::vector<uint8_t>> GetDepotKeys() {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    return g_slots[g_activeSlot].depotKeys;
}

std::string GetManifestId(uint32_t depotId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    const LuaData& active = g_slots[g_activeSlot];
    auto it = active.manifestIds.find(depotId);
    return it != active.manifestIds.end() ? it->second : "";
}

std::string GetAccessToken(uint32_t appId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    const LuaData& active = g_slots[g_activeSlot];
    auto it = active.accessTokens.find(appId);
    return it != active.accessTokens.end() ? it->second : "";
}

std::unordered_set<uint32_t> GetUnlockedApps() {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    return g_slots[g_activeSlot].unlockedApps;
}

std::vector<uint32_t> GetAllDepotIds() {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    const LuaData& active = g_slots[g_activeSlot];
    std::unordered_set<uint32_t> unique(active.unlockedApps.begin(), active.unlockedApps.end());
    for (const auto& [depotId, _] : active.depotKeys) {
        unique.insert(depotId);
    }
    return std::vector<uint32_t>(unique.begin(), unique.end());
}
std::vector<std::string> GetInjectModules(uint32_t appId) {
    std::lock_guard<std::mutex> lock(g_luaMutex);
    const LuaData& active = g_slots[g_activeSlot];
    auto it = active.injectModules.find(appId);
    if (it != active.injectModules.end()) {
        return it->second;
    }
    auto itGlobal = active.injectModules.find(0);
    if (itGlobal != active.injectModules.end()) {
        return itGlobal->second;
    }
    return {};
}

} // namespace LuaConfig
