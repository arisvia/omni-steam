#include <algorithm>
#include <cctype>
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

    if (KeyName) {
        std::string name(KeyName);
        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        size_t keyPos = lowerName.find("decryptionkey");
        if (keyPos != std::string::npos) {
            std::string depotIdStr;
            if (keyPos > 0) {
                size_t endDigits = lowerName.find_last_not_of("/\\ ", keyPos - 1);
                if (endDigits != std::string::npos && std::isdigit(static_cast<unsigned char>(lowerName[endDigits]))) {
                    size_t startDigits = lowerName.find_last_not_of("0123456789", endDigits);
                    depotIdStr = (startDigits == std::string::npos)
                                     ? lowerName.substr(0, endDigits + 1)
                                     : lowerName.substr(startDigits + 1, endDigits - startDigits);
                }
            }
            if (depotIdStr.empty()) {
                size_t startDigits = lowerName.find_first_of("0123456789", keyPos + 13);
                if (startDigits != std::string::npos) {
                    size_t endDigits = lowerName.find_first_not_of("0123456789", startDigits);
                    depotIdStr = (endDigits == std::string::npos)
                                     ? lowerName.substr(startDigits)
                                     : lowerName.substr(startDigits, endDigits - startDigits);
                }
            }

            if (!depotIdStr.empty()) {
                try {
                    uint32_t depotId = static_cast<uint32_t>(std::stoul(depotIdStr));
                    auto key = LuaConfig::GetDecryptionKey(depotId);
                    if (!key.empty()) {
                        if (Key && KeySize >= key.size()) {
                            spdlog::info("Hooks_Decryption: Injected depot decryption key for depot {} ({} bytes)",
                                         depotId, key.size());
                            std::memcpy(Key, key.data(), key.size());
                            return static_cast<int32_t>(key.size());
                        } else if (!Key || KeySize == 0) {
                            return static_cast<int32_t>(key.size());
                        }
                    }
                } catch (...) {
                }
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
