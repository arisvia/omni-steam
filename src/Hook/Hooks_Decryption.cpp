#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"

#include "Hook/HookMacros.h"

namespace {

void* g_pConfigStoreLocal = nullptr;

HOOK_FUNC(ConfigStoreGetBinary, int32_t, void* pObject, EConfigStore eConfigStore, const char* KeyName, char* Key,
          uint32_t KeySize) {
    if (eConfigStore == k_EConfigStoreUserLocal && pObject && !g_pConfigStoreLocal) {
        g_pConfigStoreLocal = pObject;
        spdlog::debug("Captured local ConfigStore instance at {:p}", g_pConfigStoreLocal);
    }

    std::string name(KeyName ? KeyName : "");
    // Handles both Windows "\\" and POSIX "/" path separators
    size_t last = name.find("/DecryptionKey");
    if (last == std::string::npos) {
        last = name.find("\\DecryptionKey");
    }

    if (last != std::string::npos) {
        size_t start = name.find_last_of("/\\", last - 1);
        if (start != std::string::npos) {
            try {
                uint32_t depotId = std::stoul(name.substr(start + 1, last - start - 1));
                auto key = LuaConfig::GetDecryptionKey(depotId);
                if (!key.empty()) {
                    if (KeySize >= key.size()) {
                        spdlog::info("Injecting depot decryption key for depot {}: {} bytes", depotId, key.size());
                        std::memcpy(Key, key.data(), key.size());
                        return static_cast<int32_t>(key.size());
                    } else {
                        spdlog::warn("Key buffer size ({} bytes) too small for depot {}", KeySize, depotId);
                    }
                }
            } catch (...) {
            }
        }
    }

    return oConfigStoreGetBinary ? oConfigStoreGetBinary(pObject, eConfigStore, KeyName, Key, KeySize) : 0;
}

} // namespace

namespace Hooks_Decryption {

void Install() {
    uintptr_t fnAddress = PatternLoader::GetFunctionAddress("ConfigStore_GetBinary");
    if (fnAddress != 0) {
        ATTACH_HOOK(fnAddress, ConfigStoreGetBinary);
        spdlog::info("Hooks_Decryption: Successfully installed ConfigStore_GetBinary hook at {:p}",
                     reinterpret_cast<void*>(fnAddress));
    } else {
        spdlog::warn("Hooks_Decryption: ConfigStore_GetBinary signature not resolved");
    }
}

void Uninstall() {}

} // namespace Hooks_Decryption
