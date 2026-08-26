#include "WebDavClient.h"

#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>

#include <winhttp.h>

namespace Manager {

namespace {
std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty())
        return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (size <= 0)
        return L"";
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), size);
    return result;
}

struct ParsedUrl {
    std::wstring host;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
    std::wstring path;
    bool isHttps = false;
};

ParsedUrl ParseUrl(const std::string& urlStr) {
    ParsedUrl result;
    std::wstring wUrl = Utf8ToWide(urlStr);
    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.length()), 0, &urlComp)) {
        result.host = std::wstring(urlComp.lpszHostName, urlComp.dwHostNameLength);
        result.port = urlComp.nPort;
        result.isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
        if (urlComp.dwUrlPathLength > 0) {
            result.path = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        }
    }
    return result;
}

std::wstring BuildRemotePath(const std::wstring& basePath, const std::string& subPath) {
    std::wstring res = basePath;
    if (!res.empty() && res.back() != L'/')
        res += L'/';
    std::wstring p = Utf8ToWide(subPath);
    if (!p.empty() && p.front() == L'/')
        p = p.substr(1);
    return res + p;
}

WebDavResponse WinHttpRequest(const WebDavConfig& config, const std::string& remotePath, const wchar_t* method,
                              const void* bodyData = nullptr, size_t bodySize = 0,
                              const wchar_t* extraHeader = nullptr) {
    WebDavResponse resp;
    ParsedUrl base = ParseUrl(config.serverUrl);
    if (base.host.empty()) {
        resp.error = "Invalid server URL";
        return resp;
    }

    std::wstring fullPath = BuildRemotePath(base.path, remotePath);
    HINTERNET hSession = WinHttpOpen(L"OmniSteam-WebDAV/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        resp.error = "WinHttpOpen failed";
        return resp;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, base.host.c_str(), base.port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        resp.error = "WinHttpConnect failed";
        return resp;
    }

    DWORD flags = base.isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest =
        WinHttpOpenRequest(hConnect, method, fullPath.c_str(), nullptr, nullptr, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (extraHeader) {
        WinHttpAddRequestHeaders(hRequest, extraHeader, static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
    }
    if (!config.username.empty()) {
        std::wstring user = Utf8ToWide(config.username);
        std::wstring pass = Utf8ToWide(config.password);
        WinHttpSetCredentials(hRequest, WINHTTP_AUTH_TARGET_SERVER, WINHTTP_AUTH_SCHEME_BASIC, user.c_str(),
                              pass.c_str(), nullptr);
    }

    BOOL sendOk = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)bodyData, (DWORD)bodySize,
                                     (DWORD)bodySize, 0);
    if (sendOk && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        resp.statusCode = static_cast<int>(statusCode);

        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
            std::vector<char> buffer(bytesAvailable);
            DWORD bytesRead = 0;
            if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
                resp.body.append(buffer.data(), bytesRead);
            }
        }
    } else {
        resp.error = "WinHttp transaction failed";
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}
} // namespace

WebDavResponse WebDavClient::MkCol(const WebDavConfig& config, const std::string& remotePath) {
    return WinHttpRequest(config, remotePath, L"MKCOL");
}

WebDavResponse WebDavClient::UploadFile(const WebDavConfig& config, const std::string& remotePath,
                                        const std::vector<uint8_t>& data) {
    return WinHttpRequest(config, remotePath, L"PUT", data.data(), data.size());
}

WebDavResponse WebDavClient::DownloadFile(const WebDavConfig& config, const std::string& remotePath) {
    return WinHttpRequest(config, remotePath, L"GET");
}

WebDavResponse WebDavClient::Delete(const WebDavConfig& config, const std::string& remotePath) {
    return WinHttpRequest(config, remotePath, L"DELETE");
}

WebDavResponse WebDavClient::PropFind(const WebDavConfig& config, const std::string& remotePath) {
    return WinHttpRequest(config, remotePath, L"PROPFIND", nullptr, 0, L"Depth: 1\r\n");
}

} // namespace Manager

#else
#include <curl/curl.h>

namespace Manager {

namespace {
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    reinterpret_cast<std::string*>(userp)->append(reinterpret_cast<char*>(contents), total);
    return total;
}

std::string BuildFullUrl(const WebDavConfig& config, const std::string& remotePath) {
    std::string base = config.serverUrl;
    if (!base.empty() && base.back() != '/')
        base += '/';
    std::string path = remotePath;
    if (!path.empty() && path.front() == '/')
        path = path.substr(1);
    return base + path;
}

void SetupAuth(CURL* curl, const WebDavConfig& config) {
    if (!config.username.empty()) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC | CURLAUTH_DIGEST);
        std::string userpwd = config.username + ":" + config.password;
        curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
}

enum class WebDavMethod { MkCol, Put, Get, Delete, PropFind };

const char* MethodName(WebDavMethod method) {
    switch (method) {
        case WebDavMethod::MkCol:
            return "MKCOL";
        case WebDavMethod::Put:
            return "PUT";
        case WebDavMethod::Get:
            return "GET";
        case WebDavMethod::Delete:
            return "DELETE";
        case WebDavMethod::PropFind:
            return "PROPFIND";
    }
    return "GET";
}

struct UploadCtx {
    const std::vector<uint8_t>* data;
    size_t pos;
};

size_t ReadCallback(char* dest, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<UploadCtx*>(userdata);
    size_t want = size * nmemb;
    size_t left = ctx->data->size() - ctx->pos;
    size_t count = want < left ? want : left;
    std::memcpy(dest, ctx->data->data() + ctx->pos, count);
    ctx->pos += count;
    return count;
}

WebDavResponse Perform(WebDavMethod method, const WebDavConfig& config, const std::string& remotePath,
                       const std::vector<uint8_t>* uploadData = nullptr) {
    WebDavResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.error = "curl init failed";
        return resp;
    }

    std::string url = BuildFullUrl(config, remotePath);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, MethodName(method));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    SetupAuth(curl, config);

    curl_slist* headers = nullptr;
    UploadCtx uploadCtx{uploadData, 0};
    if (method == WebDavMethod::PropFind) {
        headers = curl_slist_append(headers, "Depth: 1");
    }
    if (method == WebDavMethod::Put && uploadData) {
        headers = curl_slist_append(headers, "Expect:");
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, ReadCallback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &uploadCtx);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(uploadData->size()));
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);
    } else {
        resp.error = curl_easy_strerror(res);
    }

    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    return resp;
}

} // namespace

WebDavResponse WebDavClient::MkCol(const WebDavConfig& config, const std::string& remotePath) {
    return Perform(WebDavMethod::MkCol, config, remotePath);
}

WebDavResponse WebDavClient::UploadFile(const WebDavConfig& config, const std::string& remotePath,
                                        const std::vector<uint8_t>& data) {
    return Perform(WebDavMethod::Put, config, remotePath, &data);
}

WebDavResponse WebDavClient::DownloadFile(const WebDavConfig& config, const std::string& remotePath) {
    return Perform(WebDavMethod::Get, config, remotePath);
}

WebDavResponse WebDavClient::Delete(const WebDavConfig& config, const std::string& remotePath) {
    return Perform(WebDavMethod::Delete, config, remotePath);
}

WebDavResponse WebDavClient::PropFind(const WebDavConfig& config, const std::string& remotePath) {
    return Perform(WebDavMethod::PropFind, config, remotePath);
}

} // namespace Manager
#endif
