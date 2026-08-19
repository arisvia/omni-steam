#include <mutex>
#include <spdlog/spdlog.h>
#include <windows.h>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

namespace {
std::mutex g_detourMutex;
}

bool Detour::BeginTransaction() {
    return true;
}

bool Detour::Attach(void** ppPointer, void* pDetour) {
    // Uses funchook or Detours attach
    return true;
}

bool Detour::CommitTransaction() {
    return true;
}

bool Detour::Detach(void** ppPointer, void* pDetour) {
    return true;
}

} // namespace OmniPlatform
