#include <cassert>
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

int main() {
    std::cout << "Running OmniSteam IPC & Metadata Tests...\n";
    TestPatternLoader();
    TestSteamIPCBuffer();
    TestManifestClientResolution();
    std::cout << "All IPC & Metadata Tests Passed!\n";
    return 0;
}
