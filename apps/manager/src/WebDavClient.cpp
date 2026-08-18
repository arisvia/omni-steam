#include "WebDavClient.h"
#include "OmniPlatform/OmniPlatform.h"
#include <curl/curl.h>
#include <spdlog/spdlog.h>

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
