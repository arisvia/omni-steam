#include "Hooks_CallBack.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/SteamTypes.h"

#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Security/AntiCheatGuard.h"

#include "Hook/HookMacros.h"

namespace {

struct SyntheticEntry {
    HSteamUser hUser;
    int32_t iCallback;
    std::vector<uint8_t> data;
};

std::deque<SyntheticEntry> g_syntheticQueue;
std::mutex g_queueMutex;
std::atomic<bool> g_lastCallbackSynthetic{false};

HOOK_FUNC(Steam_BGetCallback, bool, HSteamPipe hSteamPipe, CallbackMsg_t* pCallbackMsg) {
    if (pCallbackMsg) {
        SyntheticEntry entry;
        bool haveSynthetic = false;
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            if (!g_syntheticQueue.empty()) {
                entry = std::move(g_syntheticQueue.front());
                g_syntheticQueue.pop_front();
                haveSynthetic = true;
            }
        }

        if (haveSynthetic) {
            static thread_local std::vector<uint8_t> s_paramBuffer;
            s_paramBuffer = std::move(entry.data);

            pCallbackMsg->m_hSteamUser = entry.hUser;
            pCallbackMsg->m_iCallback = entry.iCallback;
            pCallbackMsg->m_pubParam = s_paramBuffer.empty() ? nullptr : s_paramBuffer.data();
            pCallbackMsg->m_cubParam = static_cast<int32_t>(s_paramBuffer.size());

            g_lastCallbackSynthetic.store(true, std::memory_order_release);
            spdlog::debug("Hooks_CallBack: Dispatched synthetic callback ID {}", entry.iCallback);
            return true;
        }
    }

    g_lastCallbackSynthetic.store(false, std::memory_order_release);
    bool result = oSteam_BGetCallback ? oSteam_BGetCallback(hSteamPipe, pCallbackMsg) : false;

    return result;
}

HOOK_FUNC(Steam_FreeLastCallback, void, HSteamPipe hSteamPipe) {
    if (g_lastCallbackSynthetic.load(std::memory_order_acquire)) {
        g_lastCallbackSynthetic.store(false, std::memory_order_release);
        return; // Synthetic callback buffer was our own thread_local; do not pass to native
    }
    if (oSteam_FreeLastCallback) {
        oSteam_FreeLastCallback(hSteamPipe);
    }
}

} // namespace

namespace Hooks_CallBack {

void PostSyntheticCallback(HSteamUser hUser, int32_t iCallback, const void* pData, size_t cubData) {
    SyntheticEntry entry;
    entry.hUser = hUser;
    entry.iCallback = iCallback;
    if (pData && cubData > 0) {
        const auto* bytes = static_cast<const uint8_t*>(pData);
        entry.data.assign(bytes, bytes + cubData);
    }
    std::lock_guard<std::mutex> lock(g_queueMutex);
    if (g_syntheticQueue.size() < 256) {
        g_syntheticQueue.push_back(std::move(entry));
    }
}

void PostLicensesUpdated(HSteamUser hUser) {
    LicensesUpdated_t param{};
    PostSyntheticCallback(hUser, k_iCallback_LicensesUpdated, &param, sizeof(param));
}

size_t GetPendingSyntheticCallbackCount() {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    return g_syntheticQueue.size();
}

void ClearSyntheticCallbacks() {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_syntheticQueue.clear();
}

void Install() {
    uintptr_t fnGet = PatternLoader::GetFunctionAddress("Steam_BGetCallback");
    if (fnGet) {
        ATTACH_HOOK(fnGet, Steam_BGetCallback);
        spdlog::info("Hooks_CallBack: Successfully installed Steam_BGetCallback hook at {:p}",
                     reinterpret_cast<void*>(fnGet));
    } else {
        spdlog::warn("Hooks_CallBack: Steam_BGetCallback symbol not resolved (hooks dormant)");
    }

    uintptr_t fnFree = PatternLoader::GetFunctionAddress("Steam_FreeLastCallback");
    if (fnFree) {
        ATTACH_HOOK(fnFree, Steam_FreeLastCallback);
        spdlog::info("Hooks_CallBack: Successfully installed Steam_FreeLastCallback hook at {:p}",
                     reinterpret_cast<void*>(fnFree));
    }
}

void Uninstall() {
    uintptr_t fnGet = PatternLoader::GetFunctionAddress("Steam_BGetCallback");
    if (fnGet) {
        DETACH_HOOK(fnGet, Steam_BGetCallback);
    }
    uintptr_t fnFree = PatternLoader::GetFunctionAddress("Steam_FreeLastCallback");
    if (fnFree) {
        DETACH_HOOK(fnFree, Steam_FreeLastCallback);
    }
}

} // namespace Hooks_CallBack
