#include <windows.h>

#include <sstream>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

namespace {
std::vector<int16_t> ParsePattern(const std::string& pattern) {
    std::vector<int16_t> bytes;
    std::istringstream stream(pattern);
    std::string byteStr;

    while (stream >> byteStr) {
        if (byteStr == "?" || byteStr == "??") {
            bytes.push_back(-1);
        } else {
            bytes.push_back(static_cast<int16_t>(std::stoul(byteStr, nullptr, 16)));
        }
    }
    return bytes;
}
} // namespace

uintptr_t ByteSearch::FindPattern(uintptr_t start, size_t length, const std::string& pattern) {
    auto patternBytes = ParsePattern(pattern);
    if (patternBytes.empty() || length < patternBytes.size())
        return 0;

    const uint8_t* memory = reinterpret_cast<const uint8_t*>(start);
    size_t patternSize = patternBytes.size();

    for (size_t i = 0; i <= length - patternSize; ++i) {
        bool match = true;
        for (size_t j = 0; j < patternSize; ++j) {
            if (patternBytes[j] != -1 && memory[i + j] != static_cast<uint8_t>(patternBytes[j])) {
                match = false;
                break;
            }
        }
        if (match)
            return start + i;
    }
    return 0;
}

uintptr_t ByteSearch::FindPatternInModule(const std::string& moduleName, const std::string& pattern) {
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!BinaryParser::GetModuleTextSection(moduleName, textStart, textSize))
        return 0;
    return FindPattern(textStart, textSize, pattern);
}

bool DirectoryWatch::StartWatch(const std::vector<std::string>& directories, Callback onChange) {
    return true;
}

void DirectoryWatch::StopWatch() {}

Http::Response Http::Get(const std::string& url, int timeoutMs) {
    return {};
}

Http::Response Http::Post(const std::string& url, const std::string& body, const std::string& contentType,
                          int timeoutMs) {
    return {};
}

std::string Hash::Sha256(const std::vector<uint8_t>& data) {
    return "";
}
std::string Hash::Sha256File(const std::string& filePath) {
    return "";
}
std::string Hash::Md5(const std::vector<uint8_t>& data) {
    return "";
}

bool CredentialStore::WriteTicket(uint32_t appId, const std::string& ticketName, const std::string& hexValue) {
    HKEY hKey;
    std::string subKey = "Software\\Valve\\Steam\\Apps\\" + std::to_string(appId);
    if (RegCreateKeyExA(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) ==
        ERROR_SUCCESS) {
        RegSetValueExA(hKey, ticketName.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(hexValue.c_str()),
                       static_cast<DWORD>(hexValue.length() + 1));
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

std::string CredentialStore::ReadTicket(uint32_t appId, const std::string& ticketName) {
    HKEY hKey;
    std::string subKey = "Software\\Valve\\Steam\\Apps\\" + std::to_string(appId);
    if (RegOpenKeyExA(HKEY_CURRENT_USER, subKey.c_str(), 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        char buf[2048] = {0};
        DWORD bufSize = sizeof(buf);
        DWORD type = REG_SZ;
        if (RegQueryValueExA(hKey, ticketName.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buf), &bufSize) ==
            ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::string(buf);
        }
        RegCloseKey(hKey);
    }
    return "";
}

std::string CredentialStore::GetStoragePath() {
    return "HKCU\\Software\\Valve\\Steam\\Apps";
}

} // namespace OmniPlatform
