#include <cstdint>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"

#include "Hook/HookMacros.h"

namespace {

HOOK_FUNC(CheckAppOwnership, bool, void* pObj, uint32_t appId, AppOwnership* pOwn) {
    bool result = oCheckAppOwnership ? oCheckAppOwnership(pObj, appId, pOwn) : false;

    if (LuaConfig::HasApp(appId) || LuaConfig::HasDepot(appId)) {
        if (pOwn) {
            pOwn->PackageId = 0;
            pOwn->ReleaseState = EAppReleaseState::Released;
            pOwn->bOwnsLicense = true;
            pOwn->bFreeLicense = false;
        }
        return true;
    }
    return result;
}

} // namespace

namespace Hooks_Package {

void Install() {
    uintptr_t fnAddress = PatternLoader::GetFunctionAddress("CheckAppOwnership");
    if (fnAddress != 0) {
        ATTACH_HOOK(fnAddress, CheckAppOwnership);
        spdlog::info("Hooks_Package: Successfully installed CheckAppOwnership hook at {:p}",
                     reinterpret_cast<void*>(fnAddress));
    } else {
        spdlog::warn("Hooks_Package: CheckAppOwnership signature not resolved");
    }
}

void Uninstall() {}

} // namespace Hooks_Package
