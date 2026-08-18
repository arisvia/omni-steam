#include "OmniPlatform/OmniPlatform.h"
#include <funchook.h>
#include <mutex>
#include <spdlog/spdlog.h>

namespace OmniPlatform {

namespace {
    funchook_t* g_funchook = nullptr;
    std::mutex g_detourMutex;
}

bool Detour::BeginTransaction() {
    std::lock_guard<std::mutex> lock(g_detourMutex);
    if (g_funchook) return true;
    g_funchook = funchook_create();
    return g_funchook != nullptr;
}

bool Detour::Attach(void** ppPointer, void* pDetour) {
    std::lock_guard<std::mutex> lock(g_detourMutex);
    if (!g_funchook) {
        g_funchook = funchook_create();
        if (!g_funchook) return false;
    }
    int rv = funchook_prepare(g_funchook, ppPointer, pDetour);
    if (rv != 0) {
        spdlog::error("funchook_prepare failed: {}", funchook_error_message(g_funchook));
        return false;
    }
    return true;
}

bool Detour::CommitTransaction() {
    std::lock_guard<std::mutex> lock(g_detourMutex);
    if (!g_funchook) return false;
    int rv = funchook_install(g_funchook, 0);
    if (rv != 0) {
        spdlog::error("funchook_install failed: {}", funchook_error_message(g_funchook));
        funchook_destroy(g_funchook);
        g_funchook = nullptr;
        return false;
    }
    return true;
}

bool Detour::Detach(void** ppPointer, void* pDetour) {
    std::lock_guard<std::mutex> lock(g_detourMutex);
    if (!g_funchook) return false;
    int rv = funchook_uninstall(g_funchook, 0);
    funchook_destroy(g_funchook);
    g_funchook = nullptr;
    return rv == 0;
}

} // namespace OmniPlatform
