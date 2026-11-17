#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace HookStatus {

// Builds the core_status.json payload from a full hook registry snapshot and
// the resolved-anchor snapshot. Pure serialization (no file I/O, no global
// state) so the smoke regression tests can exercise it directly.
// Legacy Manager keys (CheckAppOwnership / ConfigStore_GetBinary /
// IPCProcessMessage) are always emitted, even when never attempted, so older
// Manager builds keep parsing the document.
std::string BuildCoreStatusJson(uint32_t pid, const std::string& version, const std::string& commit,
                                const std::string& targetModule, size_t luaCount, int64_t timestamp,
                                const std::vector<std::pair<std::string, bool>>& hooks,
                                const std::vector<std::pair<std::string, uintptr_t>>& anchors);

} // namespace HookStatus