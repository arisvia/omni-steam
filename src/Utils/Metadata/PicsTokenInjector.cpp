#include "PicsTokenInjector.h"

#include <cstdint>
#include <string>
#include <vector>

#include "Utils/Config/LuaConfig.h"

namespace PicsTokenInjector {

namespace {

constexpr uint8_t kAppFieldTag = 0x0A;    // field 1, wire type 2
constexpr uint8_t kAccessTokenTag = 0x10; // field 2, wire type 0
constexpr uint32_t kMaxTokenString = 32;

bool ReadVarint(const uint8_t*& ptr, const uint8_t* end, uint64_t& out) {
    out = 0;
    int shift = 0;
    while (ptr < end && shift < 64) {
        uint8_t b = *ptr++;
        out |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
            return true;
        shift += 7;
    }
    return shift >= 64 ? false : ptr == end;
}

void WriteVarint(std::vector<uint8_t>& buf, uint64_t value) {
    while (value >= 0x80) {
        buf.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(value & 0x7F));
}

uint64_t ResolveConfiguredToken(uint32_t appId) {
    if (!LuaConfig::HasApp(appId) && !LuaConfig::HasDepot(appId))
        return 0;

    std::string token = LuaConfig::GetAccessToken(appId);
    if (token.empty() || token.size() > kMaxTokenString)
        return 0;
    try {
        size_t consumed = 0;
        uint64_t value = std::stoull(token, &consumed);
        if (consumed != token.size())
            return 0;
        return value;
    } catch (...) {
        return 0;
    }
}

// Appends the configured access_token to one App entry when needed.
// Returns false when the entry is malformed or already carries the right token.
bool PatchAppEntry(const uint8_t* data, uint64_t len, std::vector<uint8_t>& out) {
    const uint8_t* ptr = data;
    const uint8_t* end = data + len;

    uint32_t appId = 0;
    bool hasAppId = false;
    uint64_t currentToken = 0;
    bool hasCurrentToken = false;

    while (ptr < end) {
        uint64_t tag = 0;
        if (!ReadVarint(ptr, end, tag))
            return false;
        uint32_t fieldNumber = static_cast<uint32_t>(tag >> 3);
        uint32_t wireType = static_cast<uint32_t>(tag & 7);

        if (wireType == 0) {
            uint64_t value = 0;
            if (!ReadVarint(ptr, end, value))
                return false;
            if (fieldNumber == 1) {
                appId = static_cast<uint32_t>(value);
                hasAppId = true;
            } else if (fieldNumber == 2) {
                currentToken = value;
                hasCurrentToken = true;
            }
        } else if (wireType == 1) {
            if (end - ptr < 8)
                return false;
            ptr += 8;
        } else if (wireType == 5) {
            if (end - ptr < 4)
                return false;
            ptr += 4;
        } else if (wireType == 2) {
            uint64_t nestedLen = 0;
            if (!ReadVarint(ptr, end, nestedLen))
                return false;
            if (nestedLen > static_cast<uint64_t>(end - ptr))
                return false;
            ptr += static_cast<size_t>(nestedLen);
        } else {
            return false;
        }
    }

    if (!hasAppId || appId == 0)
        return false;

    uint64_t desiredToken = ResolveConfiguredToken(appId);
    if (desiredToken == 0)
        return false;
    if (hasCurrentToken && currentToken == desiredToken)
        return false;

    out.assign(data, data + len);
    out.push_back(kAccessTokenTag);
    WriteVarint(out, desiredToken);
    return true;
}

} // namespace

bool PatchProductInfoRequest(const uint8_t* body, uint32_t cbBody, std::vector<uint8_t>& out) {
    constexpr uint32_t kMaxInputBytes = 1u << 20;
    if (!body || cbBody == 0 || cbBody > kMaxInputBytes)
        return false;

    const uint8_t* ptr = body;
    const uint8_t* end = body + cbBody;
    std::vector<uint8_t> patched;
    patched.reserve(cbBody + 32);
    bool modified = false;

    while (ptr < end) {
        const uint8_t* fieldStart = ptr;
        uint64_t tag = 0;
        if (!ReadVarint(ptr, end, tag))
            return false;
        uint32_t wireType = static_cast<uint32_t>(tag & 7);

        if (wireType == 2) {
            uint64_t len = 0;
            if (!ReadVarint(ptr, end, len))
                return false;
            if (len > static_cast<uint64_t>(end - ptr))
                return false;
            const uint8_t* payload = ptr;
            ptr += static_cast<size_t>(len);

            std::vector<uint8_t> patchedEntry;
            if (tag == kAppFieldTag && PatchAppEntry(payload, len, patchedEntry)) {
                // Re-emit with a fresh length prefix around the patched entry.
                patched.push_back(kAppFieldTag);
                WriteVarint(patched, patchedEntry.size());
                patched.insert(patched.end(), patchedEntry.begin(), patchedEntry.end());
                modified = true;
            } else {
                patched.insert(patched.end(), fieldStart, ptr);
            }
        } else if (wireType == 0) {
            uint64_t value = 0;
            if (!ReadVarint(ptr, end, value))
                return false;
            patched.insert(patched.end(), fieldStart, ptr);
        } else if (wireType == 1) {
            if (end - ptr < 8)
                return false;
            ptr += 8;
            patched.insert(patched.end(), fieldStart, ptr);
        } else if (wireType == 5) {
            if (end - ptr < 4)
                return false;
            ptr += 4;
            patched.insert(patched.end(), fieldStart, ptr);
        } else {
            return false;
        }
    }

    if (!modified)
        return false;

    out.swap(patched);
    return true;
}

} // namespace PicsTokenInjector
