#include "omni_check.h"

#include <cstdint>
#include <iostream>
#include <set>
#include <vector>

#include "Utils/Metadata/ProtoFields.h"

namespace {

std::vector<uint8_t> Varint(uint64_t value) {
    std::vector<uint8_t> out;
    ProtoFields::WriteVarint(out, value);
    return out;
}

// Builds a message body from (fieldNumber, wireValue) varint pairs.
std::vector<uint8_t> MakeMessage(const std::vector<std::pair<uint32_t, uint64_t>>& varintFields) {
    std::vector<uint8_t> body;
    for (const auto& [field, value] : varintFields) {
        ProtoFields::AppendVarintField(body, field, value);
    }
    return body;
}

void TestVarintRoundTrip() {
    const uint64_t cases[] = {0, 1, 127, 128, 300, 0xFFFFFFFFull, 0x123456789ABCDEFull};
    for (uint64_t value : cases) {
        auto encoded = Varint(value);
        const uint8_t* ptr = encoded.data();
        const uint8_t* end = ptr + encoded.size();
        uint64_t decoded = 0;
        OMNI_CHECK(ProtoFields::ReadVarint(ptr, end, decoded));
        OMNI_CHECK(decoded == value);
        OMNI_CHECK(ptr == end);
    }
    std::cout << "[PASS] TestVarintRoundTrip\n";
}

void TestNegativeVarintIsTenBytes() {
    auto encoded = Varint(static_cast<uint64_t>(-1));
    OMNI_CHECK(encoded.size() == 10); // protobuf encodes negative int32/64 as 10-byte varint
    std::cout << "[PASS] TestNegativeVarintIsTenBytes\n";
}

void TestGetVarintAndFixed64() {
    // body: field 2 varint 730, field 4 fixed64 donor
    std::vector<uint8_t> body = MakeMessage({{2, 730}});
    ProtoFields::AppendFixed64Field(body, 4, 0x0110000100001234ull);

    auto appId = ProtoFields::GetVarintField(body.data(), static_cast<uint32_t>(body.size()), 2);
    OMNI_CHECK(appId && *appId == 730);

    uint32_t wire = 0;
    auto donor = ProtoFields::GetScalarField(body.data(), static_cast<uint32_t>(body.size()), 4, &wire);
    OMNI_CHECK(donor && wire == ProtoFields::WireFixed64);
    OMNI_CHECK(*donor == 0x0110000100001234ull);

    OMNI_CHECK(!ProtoFields::GetVarintField(body.data(), static_cast<uint32_t>(body.size()), 9));
    std::cout << "[PASS] TestGetVarintAndFixed64\n";
}

void TestAppendOverrideLastWins() {
    auto body = MakeMessage({{1, 111}});
    ProtoFields::AppendVarintField(body, 1, 222);

    // Reader semantics: the last occurrence of a scalar field wins.
    auto value = ProtoFields::GetVarintField(body.data(), static_cast<uint32_t>(body.size()), 1);
    OMNI_CHECK(value && *value == 222);
    std::cout << "[PASS] TestAppendOverrideLastWins\n";
}

void TestWithoutFields() {
    // stats=4 entries interleaved with scalars that must survive untouched.
    std::vector<uint8_t> body;
    ProtoFields::AppendVarintField(body, 1, 0xAABB); // sha_schema stand-in
    body.push_back(0x22);                            // field 4, wire 2
    ProtoFields::WriteVarint(body, 3);               // length 3
    body.push_back(0xDE);
    body.push_back(0xAD);
    body.push_back(0xBE);
    ProtoFields::AppendVarintField(body, 2, 42); // crc_stats
    body.push_back(0x22);                        // second repeated entry
    ProtoFields::WriteVarint(body, 1);
    body.push_back(0x11);

    auto pruned = ProtoFields::WithoutFields(body.data(), static_cast<uint32_t>(body.size()), std::set<uint32_t>{4});
    OMNI_CHECK(pruned);
    OMNI_CHECK(!ProtoFields::HasField(pruned->data(), static_cast<uint32_t>(pruned->size()), 4));

    auto sha = ProtoFields::GetVarintField(pruned->data(), static_cast<uint32_t>(pruned->size()), 1);
    auto crc = ProtoFields::GetVarintField(pruned->data(), static_cast<uint32_t>(pruned->size()), 2);
    OMNI_CHECK(sha && *sha == 0xAABB);
    OMNI_CHECK(crc && *crc == 42);
    std::cout << "[PASS] TestWithoutFields\n";
}

void TestMalformedInputSafe() {
    const uint8_t truncated[] = {0x08}; // tag without payload
    OMNI_CHECK(!ProtoFields::GetVarintField(truncated, sizeof(truncated), 1));
    OMNI_CHECK(!ProtoFields::HasField(truncated, sizeof(truncated), 1));
    OMNI_CHECK(!ProtoFields::WithoutFields(truncated, sizeof(truncated), std::set<uint32_t>{1}));

    const uint8_t badLength[] = {0x12, 0x40, 0x01}; // len-delimited claiming 64 bytes, only 1 present
    OMNI_CHECK(!ProtoFields::WithoutFields(badLength, sizeof(badLength), std::set<uint32_t>{}));

    OMNI_CHECK(!ProtoFields::GetVarintField(nullptr, 16, 1));
    std::cout << "[PASS] TestMalformedInputSafe\n";
}

} // namespace

int main() {
    std::cout << "Running OmniSteam Stats Proto Tests...\n";
    TestVarintRoundTrip();
    TestNegativeVarintIsTenBytes();
    TestGetVarintAndFixed64();
    TestAppendOverrideLastWins();
    TestWithoutFields();
    TestMalformedInputSafe();
    std::cout << "All StatsProtoTests Passed!\n";
    return 0;
}
