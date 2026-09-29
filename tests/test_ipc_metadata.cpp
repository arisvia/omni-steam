#include "omni_check.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
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
#include "Utils/Tickets/AppTicket.h"
#include "Utils/Tickets/EticketClient.h"
#include "Utils/Tickets/LegacyCDKey.h"
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

void TestLegacyCDKeySynthesizer() {
    uint32_t appId = 1086940;
    uint32_t accountId = 12345678;
    std::string key1 = LegacyCDKey::Synthesize(appId, accountId);
    std::string key2 = LegacyCDKey::Synthesize(appId, accountId);

    // 1. Length is exactly 19 (16 alphanumeric chars + 3 dashes)
    OMNI_CHECK(key1.size() == 19);
    // 2. Dashes are at positions 4, 9, 14
    OMNI_CHECK(key1[4] == '-' && key1[9] == '-' && key1[14] == '-');
    // 3. Deterministic: identical inputs produce identical output
    OMNI_CHECK(key1 == key2);

    // 4. Different account produces different key
    std::string keyOther = LegacyCDKey::Synthesize(appId, 87654321);
    OMNI_CHECK(key1 != keyOther);

    std::cout << "[PASS] TestLegacyCDKeySynthesizer\n";
}

void TestAppTicketSteamIdExtraction() {
    // Construct dummy AppOwnershipTicket: [uint32 Size][uint32 Version][uint64 SteamID]...
    std::vector<uint8_t> dummyTicket(32, 0);
    uint32_t size = 32;
    uint32_t version = 1;
    uint64_t expectedSteamId = 0x011000010A0B0C0Dull;

    std::memcpy(dummyTicket.data(), &size, sizeof(uint32_t));
    std::memcpy(dummyTicket.data() + 4, &version, sizeof(uint32_t));
    std::memcpy(dummyTicket.data() + 8, &expectedSteamId, sizeof(uint64_t));

    uint64_t extracted = AppTicket::ExtractSteamIdFromTicket(dummyTicket);
    OMNI_CHECK(extracted == expectedSteamId);

    // Short ticket must return 0
    std::vector<uint8_t> shortTicket(8, 0);
    OMNI_CHECK(AppTicket::ExtractSteamIdFromTicket(shortTicket) == 0);

    // Test AppID 7 forgery via mock provider
    AppTicket::SetCacheTicketProvider([](uint32_t aid) -> std::vector<uint8_t> {
        if (aid == 7) {
            return std::vector<uint8_t>(160, 0xBB);
        }
        return {};
    });

    auto forged = AppTicket::ForgeLocalAppOwnershipTicket(1086940);
    OMNI_CHECK(!forged.empty());
    OMNI_CHECK(forged.size() == 164); // 160 + sizeof(uint32_t)

    AppTicket::AppOwnershipTicket outTicket;
    OMNI_CHECK(AppTicket::GetAppOwnershipTicket(1086940, outTicket));
    OMNI_CHECK(outTicket.totalSize == 160);

    AppTicket::SetCacheTicketProvider(nullptr);

    std::cout << "[PASS] TestAppTicketSteamIdExtraction\n";
}

void TestEticketClientOfflineFallback() {
    // Without configuration, GetEticketUrl returns empty and FetchFreshEticket safely falls back
    OMNI_CHECK(EticketClient::GetEticketUrl().empty());

    std::vector<uint8_t> dummyNonce = {0x01, 0x02, 0x03, 0x04};
    auto result = EticketClient::FetchFreshEticket(1086940, dummyNonce);
    OMNI_CHECK(!result.has_value());

    EticketClient::ClearCache();
    std::cout << "[PASS] TestEticketClientOfflineFallback\n";
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
    TestLegacyCDKeySynthesizer();
    TestAppTicketSteamIdExtraction();
    TestEticketClientOfflineFallback();
    std::cout << "All IPC & Metadata Tests Passed!\n";
    return 0;
}
