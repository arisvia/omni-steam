#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"
void TestEncoding() {
    std::string hex = "5954562e";
    auto bytes = OmniPlatform::Encoding::HexToBytes(hex);
    assert(bytes.size() == 4);
    assert(bytes[0] == 0x59);
    assert(bytes[1] == 0x54);
    assert(bytes[2] == 0x56);
    assert(bytes[3] == 0x2e);

    std::string roundtrip = OmniPlatform::Encoding::BytesToHex(bytes.data(), bytes.size());
    assert(roundtrip == hex);
    std::cout << "[PASS] TestEncoding\n";
}

void TestByteSearch() {
    uint8_t buffer[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x90, 0xC3};
    uintptr_t base = reinterpret_cast<uintptr_t>(buffer);

    uintptr_t found = OmniPlatform::ByteSearch::FindPattern(base, sizeof(buffer), "55 48 89 E5 ?? 57");
    assert(found == base);

    uintptr_t notFound = OmniPlatform::ByteSearch::FindPattern(base, sizeof(buffer), "FF FF FF");
    assert(notFound == 0);
    std::cout << "[PASS] TestByteSearch\n";
}

void TestCredentialStore() {
    uint32_t appId = 1361510;
    std::string testHex = "14000000aabbccdd";

    bool ok = OmniPlatform::CredentialStore::WriteTicket(appId, "TestTicket", testHex);
    assert(ok);

    std::string readBack = OmniPlatform::CredentialStore::ReadTicket(appId, "TestTicket");
    assert(readBack == testHex);
    std::cout << "[PASS] TestCredentialStore\n";
}

int main() {
    std::cout << "Running OmniSteam Platform Tests...\n";
    TestEncoding();
    TestByteSearch();
    TestCredentialStore();
    std::cout << "All Phase 1 Platform Tests Passed!\n";
    return 0;
}
