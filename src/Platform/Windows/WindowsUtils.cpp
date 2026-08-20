#include <windows.h>

#include <algorithm>
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <winhttp.h>

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
    Response res;
    if (url.empty())
        return res;

    std::string currentUrl = url;
    int maxRedirects = 8;

    while (maxRedirects-- > 0) {
        std::wstring wUrl = Encoding::Utf8ToWide(currentUrl);
        URL_COMPONENTS urlComp{};
        urlComp.dwHostNameLength = static_cast<DWORD>(-1);
        urlComp.dwUrlPathLength = static_cast<DWORD>(-1);
        urlComp.dwExtraInfoLength = static_cast<DWORD>(-1);
        urlComp.dwSchemeLength = static_cast<DWORD>(-1);

        if (!WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.length()), 0, &urlComp)) {
            res.error = "Invalid URL format";
            return res;
        }

        std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
        std::wstring urlPath;
        if (urlComp.dwUrlPathLength > 0 && urlComp.lpszUrlPath) {
            urlPath = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        } else {
            urlPath = L"/";
        }
        if (urlComp.dwExtraInfoLength > 0 && urlComp.lpszExtraInfo) {
            urlPath += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        }

        HINTERNET hSession =
            WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) OmniSteam/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            res.error = "WinHttpOpen failed";
            return res;
        }

        if (timeoutMs > 0) {
            WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
        }

        // Enable TLS 1.2 and TLS 1.3
        DWORD secureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &secureProtocols, sizeof(secureProtocols));

        HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            res.error = "WinHttpConnect failed";
            return res;
        }

        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            res.error = "WinHttpOpenRequest failed";
            return res;
        }

        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

        if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
            DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                             SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
        }

        BOOL bResults =
            WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (bResults) {
            bResults = WinHttpReceiveResponse(hRequest, nullptr);
        }

        if (bResults) {
            DWORD statusCode = 0;
            DWORD size = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
            res.statusCode = static_cast<int>(statusCode);

            // Handle HTTP Redirects (301, 302, 303, 307, 308) across domains (e.g. GitHub to CDN)
            if (statusCode >= 300 && statusCode < 400) {
                DWORD locSize = 0;
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &locSize,
                                    WINHTTP_NO_HEADER_INDEX);
                if (locSize > 0) {
                    std::vector<wchar_t> locBuf(locSize / sizeof(wchar_t) + 1);
                    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                            locBuf.data(), &locSize, WINHTTP_NO_HEADER_INDEX)) {
                        std::string nextUrl = Encoding::WideToUtf8(locBuf.data());
                        if (nextUrl.rfind("/", 0) == 0) {
                            std::string scheme = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? "https://" : "http://";
                            std::string host = Encoding::WideToUtf8(hostName);
                            nextUrl = scheme + host + nextUrl;
                        }
                        currentUrl = nextUrl;
                        WinHttpCloseHandle(hRequest);
                        WinHttpCloseHandle(hConnect);
                        WinHttpCloseHandle(hSession);
                        continue;
                    }
                }
            }

            res.body.clear();
            DWORD dwSize = 0;
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
                    break;
                if (dwSize == 0)
                    break;

                std::vector<char> buffer(dwSize);
                DWORD dwDownloaded = 0;
                if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                    res.body.append(buffer.data(), dwDownloaded);
                } else {
                    break;
                }
            } while (dwSize > 0);
        } else {
            res.error = "WinHttp request failed (error " + std::to_string(GetLastError()) + ")";
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return res;
    }

    res.error = "Too many redirects";
    return res;
}

Http::Response Http::Post(const std::string& url, const std::string& body, const std::string& contentType,
                          int timeoutMs) {
    Response res;
    if (url.empty())
        return res;

    std::wstring wUrl = Encoding::Utf8ToWide(url);
    URL_COMPONENTS urlComp{};
    urlComp.dwHostNameLength = static_cast<DWORD>(-1);
    urlComp.dwUrlPathLength = static_cast<DWORD>(-1);
    urlComp.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.length()), 0, &urlComp)) {
        res.error = "Invalid URL format";
        return res;
    }

    std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength + urlComp.dwExtraInfoLength);

    HINTERNET hSession = WinHttpOpen(L"OmniSteam/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        res.error = "WinHttpOpen failed";
        return res;
    }

    if (timeoutMs > 0) {
        WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    }

    DWORD secureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &secureProtocols, sizeof(secureProtocols));

    HINTERNET hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        res.error = "WinHttpConnect failed";
        return res;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        res.error = "WinHttpOpenRequest failed";
        return res;
    }

    std::string headerStr = "Content-Type: " + contentType;
    std::wstring wHeaders = Encoding::Utf8ToWide(headerStr);
    BOOL bResults = WinHttpSendRequest(hRequest, wHeaders.c_str(), static_cast<DWORD>(wHeaders.length()),
                                       const_cast<char*>(body.data()), static_cast<DWORD>(body.length()),
                                       static_cast<DWORD>(body.length()), 0);
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    }

    if (bResults) {
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        res.statusCode = static_cast<int>(statusCode);

        DWORD dwSize = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
                break;
            if (dwSize == 0)
                break;

            std::vector<char> buffer(dwSize);
            DWORD dwDownloaded = 0;
            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                res.body.append(buffer.data(), dwDownloaded);
            } else {
                break;
            }
        } while (dwSize > 0);
    } else {
        res.error = "WinHttp POST failed (error " + std::to_string(GetLastError()) + ")";
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
}

std::string Hash::Sha256(const std::vector<uint8_t>& data) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return "";
    }

    DWORD hashObjectSize = 0, dataLen = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PBYTE>(&hashObjectSize), sizeof(DWORD), &dataLen, 0);
    std::vector<uint8_t> hashObject(hashObjectSize);

    DWORD hashLength = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PBYTE>(&hashLength), sizeof(DWORD), &dataLen, 0);
    std::vector<uint8_t> hash(hashLength);

    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCryptCreateHash(hAlg, &hHash, hashObject.data(), hashObjectSize, nullptr, 0, 0) == 0) {
        if (!data.empty()) {
            BCryptHashData(hHash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0);
        }
        BCryptFinishHash(hHash, hash.data(), hashLength, 0);
        BCryptDestroyHash(hHash);
    }

    BCryptCloseAlgorithmProvider(hAlg, 0);
    return Encoding::BytesToHex(hash.data(), hash.size());
}

std::string Hash::Sha256File(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file)
        return "";
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return Sha256(buffer);
}

std::string Hash::Md5(const std::vector<uint8_t>& data) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_MD5_ALGORITHM, nullptr, 0) != 0) {
        return "";
    }

    DWORD hashObjectSize = 0, dataLen = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PBYTE>(&hashObjectSize), sizeof(DWORD), &dataLen, 0);
    std::vector<uint8_t> hashObject(hashObjectSize);

    DWORD hashLength = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, reinterpret_cast<PBYTE>(&hashLength), sizeof(DWORD), &dataLen, 0);
    std::vector<uint8_t> hash(hashLength);

    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCryptCreateHash(hAlg, &hHash, hashObject.data(), hashObjectSize, nullptr, 0, 0) == 0) {
        if (!data.empty()) {
            BCryptHashData(hHash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0);
        }
        BCryptFinishHash(hHash, hash.data(), hashLength, 0);
        BCryptDestroyHash(hHash);
    }

    BCryptCloseAlgorithmProvider(hAlg, 0);
    return Encoding::BytesToHex(hash.data(), hash.size());
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
