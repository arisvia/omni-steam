#include <cstdint>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Metadata/SteamIPC.h"

#include "Hook/HookMacros.h"

namespace Hooks_IPC {

void Install() {
    // IPC inspection is currently passive; no detour hooks required to preserve zero IPC latency
}

void Uninstall() {}

} // namespace Hooks_IPC
