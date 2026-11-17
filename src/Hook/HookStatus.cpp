#include "Hook/HookStatus.h"

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace HookStatus {

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
                                const std::vector<std::pair<std::string, uintptr_t>>& anchors) {
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
         << "  \"luaFilesCount\": " << luaCount << ",\n"
         << "  \"timestamp\": " << timestamp << "\n"
         << "}\n";
    return json.str();
}

} // namespace HookStatus