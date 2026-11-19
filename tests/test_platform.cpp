#include "omni_check.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
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

void TestHash() {
    // SHA-256 keys the signature-DB cache and validates depot-key CDN downloads,
    // and each platform answers it from a different implementation (bcrypt /
    // CommonCrypto / in-house), so all three must reproduce the FIPS 180-4
    // vectors. The 55/56/64-byte inputs pin the padding boundary where
    // hand-rolled digests go wrong; the 1000-byte one covers multi-block.
    auto Bytes = [](const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    OMNI_CHECK(OmniPlatform::Hash::Sha256(Bytes("")) ==
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    OMNI_CHECK(OmniPlatform::Hash::Sha256(Bytes("abc")) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    OMNI_CHECK(OmniPlatform::Hash::Sha256(Bytes(std::string(55, 'a'))) ==
               "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    OMNI_CHECK(OmniPlatform::Hash::Sha256(Bytes(std::string(56, 'a'))) ==
               "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    OMNI_CHECK(OmniPlatform::Hash::Sha256(Bytes(std::string(1000, 'a'))) ==
               "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    OMNI_CHECK(OmniPlatform::Hash::Sha256(Bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // File hashing streams through fixed-size chunks, so it must agree with the
    // one-shot digest of the same bytes, and a missing file must come back empty
    // rather than as the hash of nothing.
    const std::string path = "omnisteam_hash_test.tmp";
    const std::string payload(20000, 'z');
    {
        std::ofstream out(path, std::ios::binary);
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
    const std::string fileHash = OmniPlatform::Hash::Sha256File(path);
    OMNI_CHECK(fileHash == OmniPlatform::Hash::Sha256(Bytes(payload)));
    OMNI_CHECK(!fileHash.empty());
    OMNI_CHECK(OmniPlatform::Hash::Sha256File("omnisteam-hash-missing-file.tmp") == "");
    std::remove(path.c_str());
    std::cout << "[PASS] TestHash\n";
}

int main() {
    std::cout << "Running OmniSteam Platform Tests...\n";
    TestClientIdentity();
    TestEncoding();
    TestByteSearch();
    TestCredentialStore();
    TestAntiCheatGuard();
    TestHash();
    std::cout << "All Phase 1 Platform Tests Passed!\n";
    return 0;
}
