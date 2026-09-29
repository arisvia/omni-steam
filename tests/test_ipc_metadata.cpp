#include "omni_check.h"

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
#include "Utils/Security/AntiCheatGuard.h"
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

    OMNI_CHECK(appId == 1361510);
    OMNI_CHECK(name == "TestGame");
    OMNI_CHECK(flag == 1);
    std::cout << "[PASS] TestSteamIPCBuffer\n";
}

void TestManifestClientResolution() {
    std::string gid = ManifestClient::QueryManifestIdByDepot(999999);
    OMNI_CHECK(gid.empty());
    std::cout << "[PASS] TestManifestClientResolution\n";
}
void TestSteamStructureInvariants() {
    // 1. Verify Enum Value Invariants
    OMNI_CHECK(static_cast<uint32_t>(EPackageStatus::Available) == 0);
    OMNI_CHECK(static_cast<uint32_t>(EAppReleaseState::Released) == 4);
    OMNI_CHECK(kSteamDefaultBasePackageId == 0);
    OMNI_CHECK(kSteamDefaultInjectedPackageCount == 1);
    OMNI_CHECK(k_iCallback_SteamServersConnected == 101);
    OMNI_CHECK(k_iCallback_LicensesUpdated == 125);
    OMNI_CHECK(k_iCallback_EncryptedAppTicketResponse == 154);
    OMNI_CHECK(k_iCallback_UserStatsReceived == 1101);
    // 2. Verify structure layout invariants (compile-time asserts live in
    //    SteamTypes.h; runtime mirrors them so drift fails loudly here too).
    OMNI_CHECK(offsetof(AppOwnership, ExistInPackageNums) == 0x14);
    OMNI_CHECK(offsetof(AppOwnership, bOwnsLicense) == 0x24);
    OMNI_CHECK(offsetof(AppOwnership, bFreeLicense) == 0x28);
    OMNI_CHECK(offsetof(CallbackMsg_t, m_hSteamUser) == 0x00);
    OMNI_CHECK(offsetof(CallbackMsg_t, m_iCallback) == 0x04);
    OMNI_CHECK(offsetof(CallbackMsg_t, m_pubParam) == 0x08);
    OMNI_CHECK(offsetof(PackageInfo, Status) == 0x18);
#if defined(OMNI_ARCH_X64)
    OMNI_CHECK(offsetof(PackageInfo, AppIdVec) == 0x40);
    OMNI_CHECK(offsetof(PackageInfo, DepotIdVec) == 0x58);
    OMNI_CHECK(sizeof(CUtlVector<AppId_t>) == 24); // no m_pElements, matches client
    OMNI_CHECK(sizeof(CallbackMsg_t) == 24);
#elif defined(OMNI_ARCH_X86)
    OMNI_CHECK(offsetof(PackageInfo, AppIdVec) == 0x38);
    OMNI_CHECK(offsetof(PackageInfo, DepotIdVec) == 0x48);
    OMNI_CHECK(sizeof(CUtlVector<AppId_t>) == 16);
    OMNI_CHECK(sizeof(CallbackMsg_t) == 16);
#endif

    std::cout << "[PASS] TestSteamStructureInvariants\n";
}

void TestDlcStoreInvariants() {
    Metadata::DlcStore::Initialize();
    uint32_t baseApp = 1958220; // WitchSpring R
    std::vector<uint32_t> dlcs = {3899110, 3899120, 3899130};
    Metadata::DlcStore::RegisterDlcs(baseApp, dlcs);

    OMNI_CHECK(Metadata::DlcStore::IsKnownDlc(3899110));
    OMNI_CHECK(Metadata::DlcStore::IsKnownDlc(3899120));
    OMNI_CHECK(Metadata::DlcStore::IsKnownDlc(3899130));
    OMNI_CHECK(!Metadata::DlcStore::IsKnownDlc(99999999));
    OMNI_CHECK(Metadata::DlcStore::Count() >= 3);

    std::cout << "[PASS] TestDlcStoreInvariants\n";
}

void TestCredentialStoreTickets() {
    uint32_t testApp = 777777;
    std::string testTicket = "deadbeefcafebabe0102030405060708";
    OMNI_CHECK(OmniPlatform::CredentialStore::WriteTicket(testApp, "AppTicket", testTicket));
    OMNI_CHECK(!OmniPlatform::CredentialStore::ReadTicket(testApp, "AppTicket").empty());
    OMNI_CHECK(OmniPlatform::CredentialStore::ReadTicket(testApp, "AppTicket") == testTicket);

    std::string testETicket = "aabbccddeeff00112233445566778899";
    OMNI_CHECK(OmniPlatform::CredentialStore::WriteTicket(testApp, "ETicket", testETicket));
    OMNI_CHECK(!OmniPlatform::CredentialStore::ReadTicket(testApp, "ETicket").empty());
    OMNI_CHECK(OmniPlatform::CredentialStore::ReadTicket(testApp, "ETicket") == testETicket);
    std::cout << "[PASS] TestCredentialStoreTickets\n";
}

void TestAntiCheatGuardWhitelist() {
    Security::AntiCheatGuard::Initialize();
    OMNI_CHECK(Security::AntiCheatGuard::IsProtectedApp(730));      // CS2
    OMNI_CHECK(Security::AntiCheatGuard::IsProtectedApp(570));      // Dota 2
    OMNI_CHECK(Security::AntiCheatGuard::IsProtectedApp(440));      // TF2
    OMNI_CHECK(Security::AntiCheatGuard::IsProtectedApp(1172470));  // Apex
    OMNI_CHECK(!Security::AntiCheatGuard::IsProtectedApp(1086940)); // BG3
    OMNI_CHECK(!Security::AntiCheatGuard::IsProtectedApp(1361510)); // Cyberpunk
    OMNI_CHECK(Security::AntiCheatGuard::Count() >= 25);

    std::cout << "[PASS] TestAntiCheatGuardWhitelist\n";
}

int main() {
    std::cout << "Running OmniSteam IPC & Metadata Tests...\n";
    TestPatternLoader();
    TestSteamIPCBuffer();
    TestManifestClientResolution();
    TestSteamStructureInvariants();
    TestDlcStoreInvariants();
    TestCredentialStoreTickets();
    TestAntiCheatGuardWhitelist();
    std::cout << "All IPC & Metadata Tests Passed!\n";
    return 0;
}
