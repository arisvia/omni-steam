#include "omni_check.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Hook/HookManager.h"
#include "Hook/HookStatus.h"

// Link stubs: the registry/serialization API under test never installs real
// hooks, but HookManager.cpp references every module's Install/Uninstall.
namespace Hooks_CallBack {
void Install() {}
void Uninstall() {}
} // namespace Hooks_CallBack
namespace Hooks_Decryption {
void Install() {}
void Uninstall() {}
} // namespace Hooks_Decryption
namespace Hooks_IPC {
void Install() {}
void Uninstall() {}
} // namespace Hooks_IPC
namespace Hooks_Manifest {
void Install() {}
void Uninstall() {}
} // namespace Hooks_Manifest
namespace Hooks_Misc {
void Install() {}
void Uninstall() {}
} // namespace Hooks_Misc
namespace Hooks_NetPacket {
void Install() {}
void Uninstall() {}
} // namespace Hooks_NetPacket
namespace Hooks_Package {
void Install() {}
void Uninstall() {}
} // namespace Hooks_Package
namespace Hooks_SteamUI {
void Install() {}
void Uninstall() {}
} // namespace Hooks_SteamUI

namespace {

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void TestHookRegistrySnapshot() {
    HookManager::SetHookActive("CheckAppOwnership", true);
    HookManager::SetHookActive("RecvPkt", false);
    HookManager::SetHookActive("FillInAppOverview", true);
    OMNI_CHECK(HookManager::IsHookActive("CheckAppOwnership"));
    OMNI_CHECK(!HookManager::IsHookActive("RecvPkt"));
    OMNI_CHECK(!HookManager::IsHookActive("NeverAttempted"));

    const auto states = HookManager::GetHookStates();
    OMNI_CHECK(states.size() == 3);
    // Sorted by name for deterministic serialization.
    OMNI_CHECK(states[0].first == "CheckAppOwnership" && states[0].second);
    OMNI_CHECK(states[1].first == "FillInAppOverview" && states[1].second);
    OMNI_CHECK(states[2].first == "RecvPkt" && !states[2].second);

    // Failed resolutions must stay visible (false), not vanish from the map.
    HookManager::SetHookActive("CheckAppOwnership", false);
    OMNI_CHECK(!HookManager::IsHookActive("CheckAppOwnership"));
    OMNI_CHECK(HookManager::GetHookStates().size() == 3);
}

void TestLegacyKeysAlwaysPresent() {
    const std::string json =
        HookStatus::BuildCoreStatusJson(1234, "1.0.0", "abc123", "steamclient64.dll", 2, 1700000000, {}, {});
    OMNI_CHECK(Contains(json, "\"active\": true"));
    OMNI_CHECK(Contains(json, "\"pid\": 1234"));
    OMNI_CHECK(Contains(json, "\"CheckAppOwnership\": false"));
    OMNI_CHECK(Contains(json, "\"ConfigStore_GetBinary\": false"));
    OMNI_CHECK(Contains(json, "\"IPCProcessMessage\": false"));
    OMNI_CHECK(Contains(json, "\"anchors\": {\n  }"));
    OMNI_CHECK(Contains(json, "\"luaFilesCount\": 2"));
    OMNI_CHECK(Contains(json, "\"timestamp\": 1700000000"));
}

void TestFullHookMapAndKeyMapping() {
    const std::vector<std::pair<std::string, bool>> hooks = {
        {"Steam_BGetCallback", true},
        {"Steam_FreeLastCallback", true},
        {"ConfigStoreGetBinary", true},
        {"IPCProcessMessage", true},
        {"SpawnProcess", true},
        {"OptedInMask", true},
        {"GetOrAddAppDataHook", false},
        {"BBuildAndAsyncSendFrame", true},
        {"RecvPkt", true},
        {"CheckAppOwnership", true},
        {"GetPackageInfo", true},
        {"FillInAppOverview", false},
    };
    const std::string json =
        HookStatus::BuildCoreStatusJson(42, "1.0.0", "abc123", "steamclient.so", 0, 1700000000, hooks, {});
    // The attach-site name is published under the legacy Manager key.
    OMNI_CHECK(Contains(json, "\"ConfigStore_GetBinary\": true"));
    OMNI_CHECK(!Contains(json, "\"ConfigStoreGetBinary\""));
    OMNI_CHECK(Contains(json, "\"GetOrAddAppDataHook\": false"));
    OMNI_CHECK(Contains(json, "\"FillInAppOverview\": false"));
    OMNI_CHECK(Contains(json, "\"Steam_BGetCallback\": true"));
    OMNI_CHECK(Contains(json, "\"BBuildAndAsyncSendFrame\": true"));
    // No trailing comma before the closing brace of the hooks object.
    OMNI_CHECK(!Contains(json, ",\n  }"));
}

void TestAnchorsHexAndZeroFilter() {
    const std::vector<std::pair<std::string, uintptr_t>> anchors = {
        {"MarkLicenseAsChanged", 0x7FF000001000ULL},
        {"ProcessPendingLicenseUpdates", 0x7FF000002000ULL},
        {"CUtlMemoryGrow", 0}, // unresolved anchors must not be published
    };
    const std::string json =
        HookStatus::BuildCoreStatusJson(42, "1.0.0", "abc123", "steamclient.dylib", 0, 1700000000, {}, anchors);
    OMNI_CHECK(Contains(json, "\"MarkLicenseAsChanged\": \"0x7ff000001000\""));
    OMNI_CHECK(Contains(json, "\"ProcessPendingLicenseUpdates\": \"0x7ff000002000\""));
    OMNI_CHECK(!Contains(json, "CUtlMemoryGrow"));
    OMNI_CHECK(!Contains(json, ",\n  }"));
}

void TestJsonEscapingAndDeterminism() {
    const std::string json = HookStatus::BuildCoreStatusJson(7, "1.0\"0", "a\\b", "steam\"client", 0, 1, {}, {});
    OMNI_CHECK(Contains(json, "\"version\": \"1.0\\\"0\""));
    OMNI_CHECK(Contains(json, "\"commit\": \"a\\\\b\""));
    OMNI_CHECK(Contains(json, "\"targetModule\": \"steam\\\"client\""));

    const std::vector<std::pair<std::string, bool>> hooks = {{"RecvPkt", true}, {"CheckAppOwnership", false}};
    const std::string a = HookStatus::BuildCoreStatusJson(7, "1.0.0", "c", "m", 0, 1, hooks, {});
    const std::string b = HookStatus::BuildCoreStatusJson(7, "1.0.0", "c", "m", 0, 1, hooks, {});
    OMNI_CHECK(a == b);
}

} // namespace

int main() {
    TestHookRegistrySnapshot();
    TestLegacyKeysAlwaysPresent();
    TestFullHookMapAndKeyMapping();
    TestAnchorsHexAndZeroFilter();
    TestJsonEscapingAndDeterminism();
    return 0;
}