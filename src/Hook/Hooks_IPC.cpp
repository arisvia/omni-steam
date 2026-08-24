#include <cstdint>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Metadata/PatternLoader.h"

#include "Hook/HookMacros.h"

namespace {

HOOK_FUNC(IPCProcessMessage, bool, void* pServer, int32_t hSteamPipe, void* pRead, void* pWrite) {
    return oIPCProcessMessage ? oIPCProcessMessage(pServer, hSteamPipe, pRead, pWrite) : false;
}

} // namespace

namespace Hooks_IPC {

void Install() {
    uintptr_t fnIPC = PatternLoader::GetFunctionAddress("IPCProcessMessage");
    if (fnIPC) {
        ATTACH_HOOK(fnIPC, IPCProcessMessage);
        spdlog::info("Hooks_IPC: Successfully installed IPCProcessMessage hook at {:p}",
                     reinterpret_cast<void*>(fnIPC));
    } else {
        spdlog::warn("Hooks_IPC: IPCProcessMessage signature not resolved");
    }
}

void Uninstall() {}

} // namespace Hooks_IPC
