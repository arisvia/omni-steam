#pragma once

#include <cstdint>
#include <string>

#include "OmniPlatform/SteamTypes.h"

namespace Hooks_Misc {

void Install();
void Uninstall();

bool IsOnlineFixActive();
AppId_t GetOnlineFixRealAppId();
void SetOnlineFixRealAppId(AppId_t appId);

} // namespace Hooks_Misc
