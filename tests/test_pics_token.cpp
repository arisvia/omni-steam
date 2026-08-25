#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PicsTokenInjector.h"

// NDEBUG strips <cassert>, which would turn every check into a no-op and let
// failures pass silently (or surface later as unrelated exceptions). This
// macro stays active in Release and prints the exact failing expression.
#define OMNI_CHECK(cond)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "[FAIL] %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);                              \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)

namespace {

std::vector<uint8_t> ToBytes(std::initializer_list<uint8_t> list) {
    return std::vector<uint8_t>(list.begin(), list.end());
}

void AppendVarint(std::vector<uint8_t>& buf, uint64_t value) {
    while (value >= 0x80) {
        buf.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(value & 0x7F));
}

// App entry: appid (field 1, varint) [+ access_token (field 2, varint)]
std::vector<uint8_t> MakeAppEntry(uint32_t appId, bool withToken = false, uint64_t token = 0) {
    std::vector<uint8_t> entry;
    entry.push_back(0x08);
    AppendVarint(entry, appId);
    if (withToken) {
        entry.push_back(0x10);
        AppendVarint(entry, token);
    }
    return entry;
}

std::vector<uint8_t> WrapAppEntry(const std::vector<uint8_t>& entry) {
    std::vector<uint8_t> body;
    body.push_back(0x0A);
    AppendVarint(body, entry.size());
    body.insert(body.end(), entry.begin(), entry.end());
    return body;
}

bool ContainsTokenField(const std::vector<uint8_t>& entry, uint64_t expected) {
    size_t i = 0;
    while (i + 1 < entry.size()) {
        if (entry[i] == 0x10) {
            ++i;
            uint64_t value = 0;
            int shift = 0;
            while (i < entry.size() && shift < 64) {
                uint8_t b = entry[i++];
                value |= static_cast<uint64_t>(b & 0x7F) << shift;
                if ((b & 0x80) == 0)
                    break;
                shift += 7;
            }
            if (value == expected)
                return true;
        } else if ((entry[i] & 0x80) == 0) {
            ++i;
        } else {
            i += 2;
        }
    }
    return false;
}

struct ScopedToken {
    ScopedToken(uint32_t appId, const std::string& token) : _appId(appId) { LuaConfig::SetAccessToken(appId, token); }
    ~ScopedToken() { LuaConfig::SetAccessToken(_appId, ""); }
    uint32_t _appId;
};

} // namespace

void TestNoTokensMeansNoPatch() {
    auto body = WrapAppEntry(MakeAppEntry(480));
    std::vector<uint8_t> out;
    OMNI_CHECK(!PicsTokenInjector::PatchProductInfoRequest(body.data(), static_cast<uint32_t>(body.size()), out));
    std::cout << "[PASS] TestNoTokensMeansNoPatch\n";
}

void TestMalformedBodyRejected() {
    const uint8_t truncated[] = {0x0A, 0x20, 0x08, 0x01};
    std::vector<uint8_t> out;
    OMNI_CHECK(!PicsTokenInjector::PatchProductInfoRequest(truncated, sizeof(truncated), out));
    OMNI_CHECK(!PicsTokenInjector::PatchProductInfoRequest(nullptr, 100, out));

    const uint8_t badWireType[] = {0x03};
    OMNI_CHECK(!PicsTokenInjector::PatchProductInfoRequest(badWireType, sizeof(badWireType), out));
    std::cout << "[PASS] TestMalformedBodyRejected\n";
}

void TestTokenInjectedForConfiguredApp() {
    ScopedToken guard(620, "12345678901234567890");

    auto origEntry = MakeAppEntry(620);
    auto body = WrapAppEntry(origEntry);
    std::vector<uint8_t> out;
    OMNI_CHECK(PicsTokenInjector::PatchProductInfoRequest(body.data(), static_cast<uint32_t>(body.size()), out));

    // Output: tag+len+patched entry; patched entry keeps the original appid
    // varint prefix and gains the token field.
    OMNI_CHECK(out.size() > body.size());
    OMNI_CHECK(out[0] == 0x0A);
    std::vector<uint8_t> entry(out.begin() + 2, out.end());
    OMNI_CHECK(entry.size() > origEntry.size());
    OMNI_CHECK(std::equal(origEntry.begin(), origEntry.end(), entry.begin()));
    OMNI_CHECK(ContainsTokenField(entry, 12345678901234567890ull));
    std::cout << "[PASS] TestTokenInjectedForConfiguredApp\n";
}

void TestExistingTokenOverridden() {
    ScopedToken guard(570, "42");

    auto body = WrapAppEntry(MakeAppEntry(570, true, 999999));
    std::vector<uint8_t> out;
    OMNI_CHECK(PicsTokenInjector::PatchProductInfoRequest(body.data(), static_cast<uint32_t>(body.size()), out));

    std::vector<uint8_t> entry(out.begin() + 2, out.end());
    OMNI_CHECK(ContainsTokenField(entry, 42));
    std::cout << "[PASS] TestExistingTokenOverridden\n";
}

void TestCorrectTokenLeftUntouched() {
    ScopedToken guard(4000, "777");

    auto body = WrapAppEntry(MakeAppEntry(4000, true, 777));
    std::vector<uint8_t> out;
    OMNI_CHECK(!PicsTokenInjector::PatchProductInfoRequest(body.data(), static_cast<uint32_t>(body.size()), out));
    std::cout << "[PASS] TestCorrectTokenLeftUntouched\n";
}

void TestUnrelatedFieldsPreserved() {
    ScopedToken guard(220200, "555555");

    std::vector<uint8_t> entry = MakeAppEntry(220200);
    std::vector<uint8_t> metaOnly; // field 1 of outer message is apps; use a sibling scalar first
    metaOnly.push_back(0x28);      // outer field 5 varint (supported non-app field)
    AppendVarint(metaOnly, 1);

    std::vector<uint8_t> body = metaOnly;
    body.push_back(0x0A);
    AppendVarint(body, entry.size());
    body.insert(body.end(), entry.begin(), entry.end());

    std::vector<uint8_t> out;
    OMNI_CHECK(PicsTokenInjector::PatchProductInfoRequest(body.data(), static_cast<uint32_t>(body.size()), out));
    OMNI_CHECK(out[0] == 0x28 && out[1] == 1); // sibling field byte-identical prefix
    OMNI_CHECK(out.size() > body.size());      // app entry grew by the token suffix
    std::cout << "[PASS] TestUnrelatedFieldsPreserved\n";
}

int main() {
    std::cout << "Running OmniSteam PICS Token Injector Tests...\n";
    const char* current = "startup";
    try {
        current = "TestNoTokensMeansNoPatch";
        TestNoTokensMeansNoPatch();
        current = "TestMalformedBodyRejected";
        TestMalformedBodyRejected();
        current = "TestTokenInjectedForConfiguredApp";
        TestTokenInjectedForConfiguredApp();
        current = "TestExistingTokenOverridden";
        TestExistingTokenOverridden();
        current = "TestCorrectTokenLeftUntouched";
        TestCorrectTokenLeftUntouched();
        current = "TestUnrelatedFieldsPreserved";
        TestUnrelatedFieldsPreserved();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[EXCEPTION] in %s: %s\n", current, e.what());
        return 2;
    } catch (...) {
        std::fprintf(stderr, "[EXCEPTION] unknown type in %s\n", current);
        return 2;
    }
    std::cout << "All PicsTokenTests Passed!\n";
    return 0;
}
