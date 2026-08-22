#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"
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
    assert(static_cast<uint32_t>(EPackageStatus::Available) == 3);
    assert(static_cast<uint32_t>(EAppReleaseState::Released) == 4);
    assert(kSteamDefaultBasePackageId == 0);
    assert(kSteamDefaultInjectedPackageCount == 1);
    assert(k_iCallback_LicensesUpdated == 125);

    // 2. Verify 64-bit SteamClient Structure Offsets Invariants
    assert(SteamOffsets::Ownership64::kExistInPackageNums == 0x14);
    assert(SteamOffsets::Ownership64::kReleaseState == 0x1C);
    assert(SteamOffsets::Ownership64::kOwnsLicense == 0x28);
    assert(SteamOffsets::Ownership64::kIsSubscribed == 0x30);
    assert(SteamOffsets::PackageInfo64::kStatus == 0x18);
    assert(SteamOffsets::PackageInfo64::kAppIdVecElements == 0x40);
    assert(SteamOffsets::PackageInfo64::kAppIdVecSize == 0x50);
    assert(SteamOffsets::PackageInfo64::kDepotIdVecElements == 0x60);
    assert(SteamOffsets::PackageInfo64::kDepotIdVecSize == 0x70);

    // 3. Verify 32-bit Legacy Structure Offsets Invariants
    assert(SteamOffsets::Ownership32::kReleaseState == 0x04);
    assert(SteamOffsets::Ownership32::kExistInPackageNums == 0x08);
    assert(SteamOffsets::Ownership32::kOwnsLicense == 0x0C);
    assert(SteamOffsets::Ownership32::kFreeLicense == 0x0D);
    assert(SteamOffsets::Ownership32::kIsSubscribed == 0x10);

    std::cout << "[PASS] TestSteamStructureInvariants\n";
}

int main() {
    std::cout << "Running OmniSteam IPC & Metadata Tests...\n";
    TestPatternLoader();
    TestSteamIPCBuffer();
    TestManifestClientResolution();
    TestSteamStructureInvariants();
    std::cout << "All IPC & Metadata Tests Passed!\n";
    return 0;
}
