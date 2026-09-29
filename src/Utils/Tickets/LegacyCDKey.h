#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace LegacyCDKey {

// Generates a deterministic Crockford base32 key (XXXX-XXXX-XXXX-XXXX, 19 chars)
// seeded by appId and accountId.
std::string Synthesize(uint32_t appId, uint32_t accountId);

// Resolves CD-Key: checks Lua override first, then generates synthesized key.
// Returns nullopt if the app is not managed.
std::optional<std::string> Resolve(uint32_t appId, uint32_t accountId);

} // namespace LegacyCDKey
