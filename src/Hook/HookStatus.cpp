#include "Hook/HookStatus.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace HookStatus {

namespace {

std::atomic<uint64_t> g_ownershipObserved{0};
std::atomic<uint64_t> g_ownershipMatched{0};
std::atomic<uint64_t> g_packetObserved{0};
std::atomic<uint64_t> g_packetMatched{0};

// The Manager may poll the status file from several threads; a torn read would
// only ever misreport a counter by one, which no gate depends on.
void Bump(std::atomic<uint64_t>& observed, std::atomic<uint64_t>& matched, bool coherent) {
    observed.fetch_add(1, std::memory_order_relaxed);
    if (coherent)
        matched.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

void RecordOwnershipProbe(bool coherent) {
    Bump(g_ownershipObserved, g_ownershipMatched, coherent);
}

void RecordPacketProbe(bool coherent) {
    Bump(g_packetObserved, g_packetMatched, coherent);
}

LayoutProbes SnapshotLayoutProbes() {
    LayoutProbes probes;
    probes.ownershipObserved = g_ownershipObserved.load(std::memory_order_relaxed);
    probes.ownershipMatched = g_ownershipMatched.load(std::memory_order_relaxed);
    probes.packetObserved = g_packetObserved.load(std::memory_order_relaxed);
    probes.packetMatched = g_packetMatched.load(std::memory_order_relaxed);
    return probes;
}

namespace {

// The Manager historically consumed "ConfigStore_GetBinary" while the hook
// registry records the attach-site name "ConfigStoreGetBinary"; keep the
// published key stable across the rename.
std::string PublishedHookKey(const std::string& name) {
    if (name == "ConfigStoreGetBinary")
        return "ConfigStore_GetBinary";
    return name;
}

} // namespace

std::string BuildCoreStatusJson(uint32_t pid, const std::string& version, const std::string& commit,
                                const std::string& targetModule, size_t luaCount, int64_t timestamp,
                                const std::vector<std::pair<std::string, bool>>& hooks,
                                const std::vector<std::pair<std::string, uintptr_t>>& anchors,
                                const LayoutProbes& layout) {
    // std::map keeps the document deterministic regardless of registry order.
    std::map<std::string, bool> hookMap;
    // Legacy keys must exist even when the hook was never attempted so older
    // Manager builds never read a missing field as an error.
    hookMap["CheckAppOwnership"] = false;
    hookMap["ConfigStore_GetBinary"] = false;
    hookMap["IPCProcessMessage"] = false;
    for (const auto& [name, active] : hooks) {
        hookMap[PublishedHookKey(name)] = active;
    }
    std::map<std::string, uintptr_t> anchorMap;
    for (const auto& [name, addr] : anchors) {
        if (addr != 0)
            anchorMap[name] = addr;
    }

    std::ostringstream json;
    json << "{\n"
         << "  \"active\": true,\n"
         << "  \"pid\": " << pid << ",\n"
         << "  \"version\": \"" << OmniPlatform::Encoding::EscapeJson(version) << "\",\n"
         << "  \"commit\": \"" << OmniPlatform::Encoding::EscapeJson(commit) << "\",\n"
         << "  \"targetModule\": \"" << OmniPlatform::Encoding::EscapeJson(targetModule) << "\",\n"
         << "  \"hooks\": {\n";
    size_t hookIndex = 0;
    for (const auto& [name, active] : hookMap) {
        json << "    \"" << OmniPlatform::Encoding::EscapeJson(name) << "\": " << (active ? "true" : "false");
        if (++hookIndex < hookMap.size())
            json << ",";
        json << "\n";
    }
    json << "  },\n"
         << "  \"anchors\": {\n";
    size_t anchorIndex = 0;
    for (const auto& [name, addr] : anchorMap) {
        json << "    \"" << OmniPlatform::Encoding::EscapeJson(name) << "\": \"0x" << std::hex << addr << std::dec
             << "\"";
        if (++anchorIndex < anchorMap.size())
            json << ",";
        json << "\n";
    }
    json << "  },\n"
         << "  \"layout\": {\n"
         << "    \"ownershipObserved\": " << layout.ownershipObserved << ",\n"
         << "    \"ownershipMatched\": " << layout.ownershipMatched << ",\n"
         << "    \"packetObserved\": " << layout.packetObserved << ",\n"
         << "    \"packetMatched\": " << layout.packetMatched << "\n"
         << "  },\n"
         << "  \"luaFilesCount\": " << luaCount << ",\n"
         << "  \"timestamp\": " << timestamp << "\n"
         << "}\n";
    return json.str();
}

} // namespace HookStatus