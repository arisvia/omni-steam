#pragma once
#include <cstddef>
#include <cstdint>

#include "OmniPlatform/SteamTypes.h"

namespace Hooks_CallBack {

void Install();
void Uninstall();

// Enqueues a synthetic callback for asynchronous delivery during the next Steam_BGetCallback pump.
// Safe across threads; never re-enters the callback dispatch loop.
void PostSyntheticCallback(HSteamUser hUser, int32_t iCallback, const void* pData, size_t cubData);

// Specifically queues LicensesUpdated_t (125) to trigger Steam UI library sidebar refresh
void PostLicensesUpdated(HSteamUser hUser = 0);

// Returns the count of pending synthetic callbacks (for diagnostics & tests)
size_t GetPendingSyntheticCallbackCount();

// Clears all pending synthetic callbacks
void ClearSyntheticCallbacks();

} // namespace Hooks_CallBack
