#include "HookManager.h"
#include "OmniPlatform/OmniPlatform.h"
#include <spdlog/spdlog.h>

namespace Hooks_Decryption { void Install(); void Uninstall(); }
namespace Hooks_IPC { void Install(); void Uninstall(); }
namespace Hooks_Manifest { void Install(); void Uninstall(); }
namespace Hooks_Package { void Install(); void Uninstall(); }

namespace HookManager {

void InstallHooks() {
    OmniPlatform::Detour::BeginTransaction();

    Hooks_Decryption::Install();
    Hooks_IPC::Install();
    Hooks_Manifest::Install();
    Hooks_Package::Install();

    if (OmniPlatform::Detour::CommitTransaction()) {
        spdlog::info("OmniSteam hooks installed successfully across current platform");
    } else {
        spdlog::error("Failed to commit hook transactions");
    }
}

void UninstallHooks() {
    Hooks_Decryption::Uninstall();
    Hooks_IPC::Uninstall();
    Hooks_Manifest::Uninstall();
    Hooks_Package::Uninstall();
    spdlog::info("OmniSteam hooks uninstalled");
}

} // namespace HookManager
