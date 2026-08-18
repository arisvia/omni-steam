#include "WebDavClient.h"
#include "OmniPlatform/OmniPlatform.h"
#include <spdlog/spdlog.h>

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
#include <winhttp.h>

namespace Manager {

namespace {
    struct ParsedUrl {
        std::wstring host;
        INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
        std::wstring path;
        bool isHttps = false;
    };

    ParsedUrl ParseUrl(const std::string& urlStr) {
        ParsedUrl result;
        std::wstring wUrl(urlStr.begin(), urlStr.end());
        URL_COMPONENTS urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwHostNameLength = (DWORD)-1;
        urlComp.dwUrlPathLength = (DWORD)-1;
        urlComp.dwExtraInfoLength = (DWORD)-1;

        if (WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
            result.host = std::wstring(urlComp.lpszHostName, urlComp.dwHostNameLength);
            result.port = urlComp.nPort;
            result.isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
            if (urlComp.dwUrlPathLength > 0) {
                result.path = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
            }
        }
        return result;
    }

    std::string BuildRemotePath(const std::string& basePath, const std::string& subPath) {
        std::string res = basePath;
        if (!res.empty() && res.back() != '/') res += '/';
        std::string p = subPath;
        if (!p.empty() && p.front() == '/') p = p.substr(1);
        return res + p;
    }

    WebDavResponse WinHttpRequest(const WebDavConfig& config, const std::string& remotePath, const wchar_t* method, const void* bodyData = nullptr, size_t bodySize = 0) {
        WebDavResponse resp;
        ParsedUrl base = ParseUrl(config.serverUrl);
        if (base.host.empty()) {
            resp.error = "Invalid server URL";
            return resp;
        }

        std::string fullPathStr = BuildRemotePath(std::string(base.path.begin(), base.path.end()), remotePath);
        std::wstring fullPath(fullPathStr.begin(), fullPathStr.end());

        HINTERNET hSession = WinHttpOpen(L"OmniSteam-WebDAV/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
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
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, fullPath.c_str(), nullptr, WINHTTP_NO_REFERRER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            resp.error = "WinHttpOpenRequest failed";
            return resp;
        }

        if (!config.username.empty()) {
            std::wstring user(config.username.begin(), config.username.end());
            std::wstring pass(config.password.begin(), config.password.end());
            WinHttpSetCredentials(hRequest, WINHTTP_AUTH_TARGET_SERVER, WINHTTP_AUTH_SCHEME_BASIC, user.c_str(), pass.c_str(), nullptr);
        }

        BOOL sendOk = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)bodyData, (DWORD)bodySize, (DWORD)bodySize, 0);
        if (sendOk && WinHttpReceiveResponse(hRequest, nullptr)) {
            DWORD statusCode = 0;
            DWORD size = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
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
}

WebDavResponse WebDavClient::MkCol(const WebDavConfig& config, const std::string& remotePath) {
    return WinHttpRequest(config, remotePath, L"MKCOL");
}

WebDavResponse WebDavClient::UploadFile(const WebDavConfig& config, const std::string& remotePath, const std::vector<uint8_t>& data) {
    return WinHttpRequest(config, remotePath, L"PUT", data.data(), data.size());
}

WebDavResponse WebDavClient::DownloadFile(const WebDavConfig& config, const std::string& remotePath) {
    return WinHttpRequest(config, remotePath, L"GET");
}

WebDavResponse WebDavClient::Delete(const WebDavConfig& config, const std::string& remotePath) {
    return WinHttpRequest(config, remotePath, L"DELETE");
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
        if (!base.empty() && base.back() != '/') base += '/';
        std::string path = remotePath;
        if (!path.empty() && path.front() == '/') path = path.substr(1);
        return base + path;
    }

    void SetupAuth(CURL* curl, const WebDavConfig& config) {
        if (!config.username.empty()) {
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC | CURLAUTH_DIGEST);
            std::string userpwd = config.username + ":" + config.password;
            curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
        }
    }
}

WebDavResponse WebDavClient::MkCol(const WebDavConfig& config, const std::string& remotePath) {
    WebDavResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    std::string url = BuildFullUrl(config, remotePath);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "MKCOL");
    SetupAuth(curl, config);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);
    } else {
        resp.error = curl_easy_strerror(res);
    }

    curl_easy_cleanup(curl);
    return resp;
}

WebDavResponse WebDavClient::UploadFile(const WebDavConfig& config, const std::string& remotePath, const std::vector<uint8_t>& data) {
    WebDavResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    std::string url = BuildFullUrl(config, remotePath);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reinterpret_cast<const char*>(data.data()));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size());
    SetupAuth(curl, config);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);
    } else {
        resp.error = curl_easy_strerror(res);
    }

    curl_easy_cleanup(curl);
    return resp;
}

WebDavResponse WebDavClient::DownloadFile(const WebDavConfig& config, const std::string& remotePath) {
    WebDavResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    std::string url = BuildFullUrl(config, remotePath);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    SetupAuth(curl, config);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);
    } else {
        resp.error = curl_easy_strerror(res);
    }

    curl_easy_cleanup(curl);
    return resp;
}

WebDavResponse WebDavClient::Delete(const WebDavConfig& config, const std::string& remotePath) {
    WebDavResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    std::string url = BuildFullUrl(config, remotePath);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    SetupAuth(curl, config);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);
    } else {
        resp.error = curl_easy_strerror(res);
    }

    curl_easy_cleanup(curl);
    return resp;
}

} // namespace Manager
#endif
