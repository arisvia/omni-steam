#include "omni_check.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/SteamTypes.h"

void TestClientIdentity() {
    // Our own surfaces announce OmniSteam. Only the upstream provider's hosts get its
    // inherited token back, because those answer a Cloudflare 403 to anything else - we
    // run no request-code or donor-SteamID service, so that traffic must keep working.
    std::string ourAgent = OmniEndpoints::Http::kUserAgent;
    OMNI_CHECK(ourAgent.rfind("OmniSteam/", 0) == 0);
    OMNI_CHECK(std::string(OmniEndpoints::Http::kUpstreamUserAgent) == "OpenSteamTool/1.0");

    OMNI_CHECK(std::string(OmniEndpoints::Http::UserAgentForUrl("https://manifest.opensteamtool.com/440")) ==
               OmniEndpoints::Http::kUpstreamUserAgent);
    OMNI_CHECK(std::string(OmniEndpoints::Http::UserAgentForUrl("https://stats.opensteamtool.com/440")) ==
               OmniEndpoints::Http::kUpstreamUserAgent);
    OMNI_CHECK(std::string(OmniEndpoints::Http::UserAgentForUrl(
                   "https://cdn.jsdelivr.net/gh/arisvia/omni-steam@data/depotkeys.bin")) ==
               OmniEndpoints::Http::kUserAgent);
    OMNI_CHECK(std::string(OmniEndpoints::Http::UserAgentForUrl("http://gmrc.wudrm.com/manifest/440")) ==
               OmniEndpoints::Http::kUserAgent);
    OMNI_CHECK(std::string(OmniEndpoints::Http::UserAgentForUrl(
                   "https://store.steampowered.com/api/appdetails?appids=440")) == OmniEndpoints::Http::kUserAgent);

    // Default preset token is branded "omnisteam", and "opensteamtool" is retained as legacy alias.
    OMNI_CHECK(std::string(OmniEndpoints::Manifest::kDefaultUpstream) == "omnisteam");
    OMNI_CHECK(std::string(OmniEndpoints::Manifest::kLegacyUpstream) == "opensteamtool");
    OMNI_CHECK(std::string(OmniEndpoints::Manifest::kSteamRunUpstream) == "steamrun");
    OMNI_CHECK(std::string(OmniEndpoints::Manifest::kWuDrmUpstream) == "wudrm");
    std::cout << "[PASS] TestClientIdentity\n";
}

void TestEncoding() {
    std::string hex = "5954562e";
    auto bytes = OmniPlatform::Encoding::HexToBytes(hex);
    OMNI_CHECK(bytes.size() == 4);
    OMNI_CHECK(bytes[0] == 0x59);
    OMNI_CHECK(bytes[1] == 0x54);
    OMNI_CHECK(bytes[2] == 0x56);
    OMNI_CHECK(bytes[3] == 0x2e);

    std::string roundtrip = OmniPlatform::Encoding::BytesToHex(bytes.data(), bytes.size());
    OMNI_CHECK(roundtrip == hex);

    const std::string escaped = OmniPlatform::Encoding::EscapeJson("C:\\Games\n\"Omni\"\t");
    OMNI_CHECK(escaped == "C:\\\\Games\\n\\\"Omni\\\"\\t");
    OMNI_CHECK(OmniPlatform::Encoding::EscapeJson(std::string("\x01", 1)) == "\\u0001");
    std::cout << "[PASS] TestEncoding\n";
}

void TestByteSearch() {
    uint8_t buffer[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x90, 0xC3};
    uintptr_t base = reinterpret_cast<uintptr_t>(buffer);

    uintptr_t found = OmniPlatform::ByteSearch::FindPattern(base, sizeof(buffer), "55 48 89 E5 ?? 57");
    OMNI_CHECK(found == base);

    uintptr_t notFound = OmniPlatform::ByteSearch::FindPattern(base, sizeof(buffer), "FF FF FF");
    OMNI_CHECK(notFound == 0);
    std::cout << "[PASS] TestByteSearch\n";
}

void TestCredentialStore() {
    uint32_t appId = 1361510;
    std::string testHex = "14000000aabbccdd";

    bool ok = OmniPlatform::CredentialStore::WriteTicket(appId, "TestTicket", testHex);
    OMNI_CHECK(ok);

    std::string readBack = OmniPlatform::CredentialStore::ReadTicket(appId, "TestTicket");
    OMNI_CHECK(readBack == testHex);
    std::cout << "[PASS] TestCredentialStore\n";
}

void TestAntiCheatGuard() {
    using namespace AntiCheat;
    OMNI_CHECK(kCounterStrike2 == 730);
    OMNI_CHECK(kDota2 == 570);
    OMNI_CHECK(kApexLegends == 1172470);
    OMNI_CHECK(kPUBG == 578080);
    OMNI_CHECK(kEldenRing == 1245620);

    std::cout << "[PASS] TestAntiCheatGuard\n";
}

int main() {
    std::cout << "Running OmniSteam Platform Tests...\n";
    TestClientIdentity();
    TestEncoding();
    TestByteSearch();
    TestCredentialStore();
    TestAntiCheatGuard();
    std::cout << "All Phase 1 Platform Tests Passed!\n";
    return 0;
}
