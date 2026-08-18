#include "Hook/HookMacros.h"
#include "Utils/Metadata/ManifestClient.h"
#include "Utils/Config/LuaConfig.h"
#include <spdlog/spdlog.h>

namespace Hooks_Manifest {

void Install() {
    spdlog::info("Hooks_Manifest: Manifest redirection engine initialized");
}

void Uninstall() {
}

} // namespace Hooks_Manifest
