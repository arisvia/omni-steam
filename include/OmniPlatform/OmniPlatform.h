#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPaths.h"
#include "OmniPlatform/SteamTypes.h"

namespace OmniPlatform {

namespace Encoding {
std::vector<uint8_t> HexToBytes(std::string_view hex);
std::string BytesToHex(const uint8_t* data, size_t length);
std::string UrlEncode(std::string_view value);
std::string UrlDecode(std::string_view in);
std::string EscapeJson(std::string_view value);
std::string WideToUtf8(std::wstring_view wide);
std::wstring Utf8ToWide(std::string_view utf8);
} // namespace Encoding

namespace Numbers {
uint64_t ParseUInt64(std::string_view str);
uint32_t ParseUInt32(std::string_view str);
} // namespace Numbers
class Detour {
public:
    static bool BeginTransaction();
    static bool CommitTransaction();
    static bool Attach(void** ppPointer, void* pDetour);
    static bool Detach(void** ppPointer, void* pDetour);
};

class DynamicLibrary {
public:
    using ModuleHandle = void*;
    static ModuleHandle Load(const std::string& path);
    static ModuleHandle GetLoadedModule(const std::string& name);
    static void* GetFunction(ModuleHandle handle, const std::string& name);
    static bool Free(ModuleHandle handle);
    static std::string GetCurrentDirectoryPath();
    static std::string GetModulePath(ModuleHandle handle);
    static uint32_t GetLastErrorCode();
};

class Memory {
public:
    static bool Protect(void* address, size_t size, uint32_t newProtect, uint32_t* oldProtect = nullptr);
    static bool Read(void* address, void* buffer, size_t size);
    static bool Write(void* address, const void* buffer, size_t size);
};

class BinaryParser {
public:
    struct SectionInfo {
        std::string name;
        uintptr_t startAddress;
        size_t size;
        uint32_t flags;
    };

    static bool GetModuleTextSection(const std::string& moduleName, uintptr_t& outStart, size_t& outSize);
    static std::vector<SectionInfo> GetSections(const std::string& modulePath);
    static uintptr_t GetModuleBase(const std::string& moduleName);
};

class ByteSearch {
public:
    static uintptr_t FindPattern(uintptr_t start, size_t length, const std::string& pattern);
    static uintptr_t FindPatternInModule(const std::string& moduleName, const std::string& pattern);
};

class DirectoryWatch {
public:
    using Callback = std::function<void(const std::string& filepath, bool isDirectory)>;
    static bool StartWatch(const std::vector<std::string>& directories, Callback onChange);
    static void StopWatch();
};

class Http {
public:
    struct Response {
        int statusCode = 0;
        std::string body;
        std::string error;
    };

    static Response Get(const std::string& url, int timeoutMs = 5000);
    static Response Post(const std::string& url, const std::string& body,
                         const std::string& contentType = "application/json", int timeoutMs = 5000);
};

class Hash {
public:
    static std::string Sha256(const std::vector<uint8_t>& data);
    static std::string Sha256File(const std::string& filePath);
    static std::string Md5(const std::vector<uint8_t>& data);
};

class Process {
public:
    static uint32_t GetCurrentProcessId();
    static std::string GetProcessName(uint32_t pid);
    static std::string GetExecutablePath();
    static std::vector<uint32_t> FindProcessIdsByName(const std::string& processName);
};

class Thread {
public:
    static void StartDetached(std::function<void()> task);
    static void Sleep(uint32_t milliseconds);
};

class CredentialStore {
public:
    static bool WriteTicket(uint32_t appId, const std::string& ticketName, const std::string& hexValue);
    static std::string ReadTicket(uint32_t appId, const std::string& ticketName);
    static std::string GetStoragePath();
};

} // namespace OmniPlatform
