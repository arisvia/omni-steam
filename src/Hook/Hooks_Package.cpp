#include <spdlog/spdlog.h>

#include "Utils/Config/LuaConfig.h"

#include "Hook/HookMacros.h"

namespace Hooks_Package {

void Install() {
    spdlog::info("Hooks_Package: License and package simulation engine initialized");
}

void Uninstall() {}

} // namespace Hooks_Package
