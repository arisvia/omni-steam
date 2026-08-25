#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>
namespace SteamIPC {

enum class EIPCCommand : uint32_t {
    InterfaceCall = 0,
    AsyncCallResult = 1,
    Callback = 2,
    Handshake = 3,
    KeepAlive = 4
};

enum class EIPCInterface : uint32_t {
    ISteamClient = 1,
    ISteamUser = 2,
    ISteamFriends = 3,
    ISteamUtils = 4,
    ISteamMatchmaking = 5,
    ISteamApps = 6,
    ISteamBilling = 7,
    IClientUser = 8,
    IClientEngine = 9
};

struct BufferReader {
    const uint8_t* data;
    size_t size;
    size_t offset = 0;

    BufferReader(const void* buf, size_t s) : data(reinterpret_cast<const uint8_t*>(buf)), size(s) {}

    bool HasMore(size_t bytes = 1) const {
        return offset <= size && bytes <= size - offset; // overflow-safe
    }

    template <typename T> bool Read(T* out) {
        if (!HasMore(sizeof(T))) {
            *out = T{};
            return false;
        }
        std::memcpy(out, data + offset, sizeof(T));
        offset += sizeof(T);
        return true;
    }

    template <typename T> T Read() {
        T val;
        Read(&val);
        return val;
    }

    std::vector<uint8_t> ReadBytes(size_t count) {
        if (!HasMore(count))
            count = size - offset;
        std::vector<uint8_t> bytes(data + offset, data + offset + count);
        offset += count;
        return bytes;
    }

    std::string ReadString() {
        uint32_t len = Read<uint32_t>();
        if (len == 0 || !HasMore(len))
            return "";
        std::string str(reinterpret_cast<const char*>(data + offset), len);
        offset += len;
        return str;
    }
};

struct BufferWriter {
    std::vector<uint8_t> buffer;

    template <typename T> void Write(T val) {
        size_t cur = buffer.size();
        buffer.resize(cur + sizeof(T));
        std::memcpy(buffer.data() + cur, &val, sizeof(T));
    }

    void WriteBytes(const void* data, size_t size) {
        size_t cur = buffer.size();
        buffer.resize(cur + size);
        std::memcpy(buffer.data() + cur, data, size);
    }

    void WriteString(const std::string& str) {
        Write<uint32_t>(static_cast<uint32_t>(str.length()));
        WriteBytes(str.data(), str.length());
    }
};

struct IPCHeader {
    uint32_t magic;
    EIPCCommand command;
    uint32_t sequence;
    uint32_t bodySize;
};

struct InterfaceCallHeader {
    EIPCInterface interfaceID;
    uint32_t funcHash;
    uint32_t fencepost;
    uint32_t argc;
};

} // namespace SteamIPC
