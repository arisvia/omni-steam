#pragma once

#include <cstdint>

namespace Hooks_Package {

void Install();
void Uninstall();
void NotifyLicenseChanged();
// True when the id is currently part of our injected Package 0 set.
// Consumed by the GetOrAddAppData skip-flag hook (Hooks_Misc).
bool IsInjectedAppId(uint32_t appId);

} // namespace Hooks_Package
