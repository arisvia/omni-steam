#include "OmniPlatform/OmniPlatform.h"
#include "Hook/HookMacros.h"
#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Metadata/SteamIPC.h"
#include <spdlog/spdlog.h>

namespace {

HOOK_FUNC(IPCProcessMessage, bool, void* pServer, int32_t hSteamPipe, void* pRead, void* pWrite) {
    if (!oIPCProcessMessage) return false;

    // Optional IPC pre-inspection
    bool result = oIPCProcessMessage(pServer, hSteamPipe, pRead, pWrite);

    // If call succeeded, inspect and filter response buffers if needed
    return result;
}

} // namespace

namespace Hooks_IPC {

void Install() {
    uintptr_t fnAddress = PatternLoader::GetFunctionAddress("IPCProcessMessage");
    if (fnAddress != 0) {
        ATTACH_HOOK(fnAddress, IPCProcessMessage);
        spdlog::info("Hooks_IPC: Successfully installed IPCProcessMessage hook at {:p}", reinterpret_cast<void*>(fnAddress));
    } else {
        spdlog::warn("Hooks_IPC: IPCProcessMessage signature not resolved");
    }
}

void Uninstall() {
}

} // namespace Hooks_IPC
