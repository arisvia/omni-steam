#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <set>
#include <vector>

// Minimal hand-rolled protobuf field manipulation for Steam CM messages.
//
// Scalar-field override semantics: protobuf parsers take the LAST occurrence
// of a non-repeated scalar field, so appending tag+value to an existing
// message overrides that field while preserving everything else byte-for-byte.
namespace ProtoFields {

constexpr uint32_t WireVarint = 0;
constexpr uint32_t WireFixed64 = 1;
constexpr uint32_t WireLengthDelim = 2;
constexpr uint32_t WireFixed32 = 5;

inline bool ReadVarint(const uint8_t*& ptr, const uint8_t* end, uint64_t& out) {
    out = 0;
    int shift = 0;
    while (ptr < end && shift < 64) {
        const uint8_t b = *ptr++;
        out |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
            return true;
        shift += 7;
    }
    return shift < 64 && ptr == end;
}

inline void WriteVarint(std::vector<uint8_t>& buf, uint64_t value) {
    while (value >= 0x80) {
        buf.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(value & 0x7F));
}

// Reads one field header. Returns false at end-of-buffer or malformed input.
inline bool ReadTag(const uint8_t*& ptr, const uint8_t* end, uint32_t& fieldNumber, uint32_t& wireType) {
    uint64_t tag = 0;
    if (!ReadVarint(ptr, end, tag))
        return false;
    fieldNumber = static_cast<uint32_t>(tag >> 3);
    wireType = static_cast<uint32_t>(tag & 7);
    return fieldNumber != 0;
}

// Skips over a field payload of the given wire type.
inline bool SkipPayload(const uint8_t*& ptr, const uint8_t* end, uint32_t wireType) {
    switch (wireType) {
        case WireVarint: {
            uint64_t ignored = 0;
            return ReadVarint(ptr, end, ignored);
        }
        case WireFixed64:
            if (end - ptr < 8)
                return false;
            ptr += 8;
            return true;
        case WireLengthDelim: {
            uint64_t len = 0;
            if (!ReadVarint(ptr, end, len))
                return false;
            if (len > static_cast<uint64_t>(end - ptr))
                return false;
            ptr += static_cast<size_t>(len);
            return true;
        }
        case WireFixed32:
            if (end - ptr < 4)
                return false;
            ptr += 4;
            return true;
        default:
            return false;
    }
}

// Finds a scalar field and returns its payload value.
// For WireVarint: the decoded varint. For WireFixed64/WireFixed32: raw LE value.
inline std::optional<uint64_t> GetScalarField(const uint8_t* body, uint32_t cbBody, uint32_t wantField,
                                              uint32_t* outWireType = nullptr) {
    const uint8_t* ptr = body;
    const uint8_t* end = body + cbBody;
    std::optional<uint64_t> result;

    while (ptr < end) {
        uint32_t field = 0, wire = 0;
        if (!ReadTag(ptr, end, field, wire))
            break;
        if (field == wantField && (wire == WireVarint || wire == WireFixed64 || wire == WireFixed32)) {
            uint64_t value = 0;
            if (wire == WireVarint) {
                if (!ReadVarint(ptr, end, value))
                    return result;
            } else if (wire == WireFixed64) {
                if (end - ptr < 8)
                    return result;
                std::memcpy(&value, ptr, 8);
                ptr += 8;
            } else {
                if (end - ptr < 4)
                    return result;
                uint32_t small = 0;
                std::memcpy(&small, ptr, 4);
                ptr += 4;
                value = small;
            }
            result = value;
            if (outWireType)
                *outWireType = wire;
            continue;
        }
        if (!SkipPayload(ptr, end, wire))
            break;
    }
    return result;
}

inline std::optional<uint64_t> GetVarintField(const uint8_t* body, uint32_t cbBody, uint32_t field) {
    uint32_t wire = 0;
    auto value = GetScalarField(body, cbBody, field, &wire);
    if (!value || wire != WireVarint)
        return std::nullopt;
    return value;
}

inline bool HasField(const uint8_t* body, uint32_t cbBody, uint32_t wantField) {
    const uint8_t* ptr = body;
    const uint8_t* end = body + cbBody;
    while (ptr < end) {
        uint32_t field = 0, wire = 0;
        if (!ReadTag(ptr, end, field, wire))
            return false;
        if (field == wantField)
            return true;
        if (!SkipPayload(ptr, end, wire))
            return false;
    }
    return false;
}

// Rebuilds the message without every occurrence of the listed field numbers.
// Returns nullopt on malformed input; otherwise the pruned serialization
// (identical bytes apart from the removed fields).
inline std::optional<std::vector<uint8_t>> WithoutFields(const uint8_t* body, uint32_t cbBody,
                                                         const std::set<uint32_t>& remove) {
    const uint8_t* ptr = body;
    const uint8_t* end = body + cbBody;
    std::vector<uint8_t> out;
    out.reserve(cbBody);

    while (ptr < end) {
        const uint8_t* fieldStart = ptr;
        uint32_t field = 0, wire = 0;
        if (!ReadTag(ptr, end, field, wire))
            return std::nullopt;
        if (!SkipPayload(ptr, end, wire))
            return std::nullopt;

        if (remove.count(field)) {
            continue;
        }
        out.insert(out.end(), fieldStart, ptr);
    }
    return out;
}

// ---- append-style overrides (last-one-wins) -----------------------------

inline void AppendVarintField(std::vector<uint8_t>& body, uint32_t field, uint64_t value) {
    WriteVarint(body, (static_cast<uint64_t>(field) << 3) | WireVarint);
    WriteVarint(body, value);
}

inline void AppendFixed64Field(std::vector<uint8_t>& body, uint32_t field, uint64_t value) {
    WriteVarint(body, (static_cast<uint64_t>(field) << 3) | WireFixed64);
    for (int i = 0; i < 8; ++i) {
        body.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

} // namespace ProtoFields
