#include "HookManager.h"

#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"

namespace Hooks_Decryption {
void Install();
void Uninstall();
} // namespace Hooks_Decryption
namespace Hooks_IPC {
void Install();
void Uninstall();
} // namespace Hooks_IPC
namespace Hooks_Manifest {
void Install();
void Uninstall();
} // namespace Hooks_Manifest
namespace Hooks_Misc {
void Install();
void Uninstall();
} // namespace Hooks_Misc
namespace Hooks_NetPacket {
void Install();
void Uninstall();
} // namespace Hooks_NetPacket
namespace Hooks_Package {
void Install();
void Uninstall();
} // namespace Hooks_Package
namespace Hooks_SteamUI {
void Install();
void Uninstall();
} // namespace Hooks_SteamUI
namespace HookManager {

void InstallHooks() {
    OmniPlatform::Detour::BeginTransaction();

    Hooks_Decryption::Install();
    Hooks_IPC::Install();
    Hooks_Manifest::Install();
    Hooks_Misc::Install();
    Hooks_NetPacket::Install();
    Hooks_Package::Install();
    Hooks_SteamUI::Install();
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
    Hooks_Misc::Uninstall();
    Hooks_NetPacket::Uninstall();
    Hooks_Package::Uninstall();
    Hooks_SteamUI::Uninstall();
}

} // namespace HookManager
