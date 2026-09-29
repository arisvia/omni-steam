#pragma once
#include <cstdint>
#include <vector>

namespace Hooks_Decryption {
void Install();
void Uninstall();
std::vector<uint8_t> GetCacheAppOwnershipTicket(uint32_t appId);
} // namespace Hooks_Decryption
