#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace HookStatus {

// Live-client evidence for the two struct layouts the hooks write into the
// client's own objects (AppOwnership at 0x24/0x28, CNetPacket at +0x10/+0x18).
// "Hook attached" and even "client survived" do not prove an offset: a hook on
// a correctly derived RVA that writes the wrong field is silent. These counters
// say what the client itself produced, and whether our interpretation of it was
// coherent, so a layout drift becomes a reported mismatch instead of a game
// that quietly never appears in the library.
//   observed: times a real client-filled struct/packet reached the hook
//   matched:  times the fields decoded at OUR offsets made sense
// matched == 0 with observed > 0 is the drift signature.
struct LayoutProbes {
    uint64_t ownershipObserved = 0;
    uint64_t ownershipMatched = 0;
    uint64_t packetObserved = 0;
    uint64_t packetMatched = 0;
};

// Lock-free: called on the client's hot paths (license check, packet receive).
void RecordOwnershipProbe(bool coherent);
void RecordPacketProbe(bool coherent);
LayoutProbes SnapshotLayoutProbes();

// Builds the core_status.json payload from a full hook registry snapshot and
// the resolved-anchor snapshot. Pure serialization (no file I/O, no global
// state) so the smoke regression tests can exercise it directly.
// Legacy Manager keys (CheckAppOwnership / ConfigStore_GetBinary /
// IPCProcessMessage) are always emitted, even when never attempted, so older
// Manager builds keep parsing the document.
std::string BuildCoreStatusJson(uint32_t pid, const std::string& version, const std::string& commit,
                                const std::string& targetModule, size_t luaCount, int64_t timestamp,
                                const std::vector<std::pair<std::string, bool>>& hooks,
                                const std::vector<std::pair<std::string, uintptr_t>>& anchors,
                                const LayoutProbes& layout = {});

} // namespace HookStatus