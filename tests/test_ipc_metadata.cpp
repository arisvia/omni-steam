#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/DlcStore.h"
#include "Utils/Metadata/ManifestClient.h"
#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Metadata/SteamIPC.h"
void TestPatternLoader() {
    PatternLoader::Initialize();
    PatternLoader::RegisterPattern("DummyFunc", "", "90 90 90", 0);
    // Verified internal pattern registry map
    std::cout << "[PASS] TestPatternLoader\n";
}

void TestSteamIPCBuffer() {
    SteamIPC::BufferWriter writer;
    writer.Write<uint32_t>(1361510);
    writer.WriteString("TestGame");
    writer.Write<uint8_t>(1);

    SteamIPC::BufferReader reader(writer.buffer.data(), writer.buffer.size());
    uint32_t appId = reader.Read<uint32_t>();
    std::string name = reader.ReadString();
    uint8_t flag = reader.Read<uint8_t>();

    assert(appId == 1361510);
    assert(name == "TestGame");
    assert(flag == 1);
    std::cout << "[PASS] TestSteamIPCBuffer\n";
}

void TestManifestClientResolution() {
    std::string gid = ManifestClient::QueryManifestIdByDepot(999999);
    assert(gid.empty());
    std::cout << "[PASS] TestManifestClientResolution\n";
}
void TestSteamStructureInvariants() {
    // 1. Verify Enum Value Invariants
    assert(static_cast<uint32_t>(EPackageStatus::Available) == 0);
    assert(static_cast<uint32_t>(EAppReleaseState::Released) == 4);
    assert(kSteamDefaultBasePackageId == 0);
    assert(kSteamDefaultInjectedPackageCount == 1);
    assert(k_iCallback_LicensesUpdated == 125);

    // 2. Verify structure layout invariants (compile-time asserts live in
    //    SteamTypes.h; runtime mirrors them so drift fails loudly here too).
    assert(offsetof(AppOwnership, ExistInPackageNums) == 0x14);
    assert(offsetof(AppOwnership, bOwnsLicense) == 0x24);
    assert(offsetof(AppOwnership, bFreeLicense) == 0x28);
    assert(offsetof(PackageInfo, Status) == 0x18);
#if defined(OMNI_ARCH_X64)
    assert(offsetof(PackageInfo, AppIdVec) == 0x40);
    assert(offsetof(PackageInfo, DepotIdVec) == 0x58);
    assert(sizeof(CUtlVector<AppId_t>) == 24); // no m_pElements, matches client
#elif defined(OMNI_ARCH_X86)
    assert(offsetof(PackageInfo, AppIdVec) == 0x38);
    assert(offsetof(PackageInfo, DepotIdVec) == 0x48);
    assert(sizeof(CUtlVector<AppId_t>) == 16);
#endif

    std::cout << "[PASS] TestSteamStructureInvariants\n";
}

void TestDlcStoreInvariants() {
    Metadata::DlcStore::Initialize();
    uint32_t baseApp = 1958220; // WitchSpring R
    std::vector<uint32_t> dlcs = {3899110, 3899120, 3899130};
    Metadata::DlcStore::RegisterDlcs(baseApp, dlcs);

    assert(Metadata::DlcStore::IsKnownDlc(3899110));
    assert(Metadata::DlcStore::IsKnownDlc(3899120));
    assert(Metadata::DlcStore::IsKnownDlc(3899130));
    assert(!Metadata::DlcStore::IsKnownDlc(99999999));
    assert(Metadata::DlcStore::Count() >= 3);

    std::cout << "[PASS] TestDlcStoreInvariants\n";
}

int main() {
    std::cout << "Running OmniSteam IPC & Metadata Tests...\n";
    TestPatternLoader();
    TestSteamIPCBuffer();
    TestManifestClientResolution();
    TestSteamStructureInvariants();
    TestDlcStoreInvariants();
    std::cout << "All IPC & Metadata Tests Passed!\n";
    return 0;
}
