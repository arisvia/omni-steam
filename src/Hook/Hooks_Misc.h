#pragma once

#include "OmniPlatform/SteamTypes.h"

namespace Hooks_Misc {

void Install();
void Uninstall();
AppId_t GetActiveAppId();
} // namespace Hooks_Misc
