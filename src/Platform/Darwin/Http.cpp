#include <curl/curl.h>
#include <string>

#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPlatform.h"
namespace OmniPlatform {

namespace {
size_t WriteCb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    reinterpret_cast<std::string*>(userp)->append(reinterpret_cast<char*>(contents), total);
    return total;
}
} // namespace

Http::Response Http::Get(const std::string& url, int timeoutMs) {
    Response res;
    CURL* curl = curl_easy_init();
    if (!curl)
        return res;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, OmniEndpoints::Http::kUserAgent);
    if (IsInsecureTlsAllowed()) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        res.statusCode = static_cast<int>(httpCode);
    } else {
        res.error = curl_easy_strerror(code);
    }

    curl_easy_cleanup(curl);
    return res;
}

Http::Response Http::Post(const std::string& url, const std::string& body, const std::string& contentType,
                          int timeoutMs) {
    Response res;
    CURL* curl = curl_easy_init();
    if (!curl)
        return res;

    struct curl_slist* headers = nullptr;
    std::string headerStr = "Content-Type: " + contentType;
    headers = curl_slist_append(headers, headerStr.c_str());

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, OmniEndpoints::Http::kUserAgent);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    if (IsInsecureTlsAllowed()) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        res.statusCode = static_cast<int>(httpCode);
    } else {
        res.error = curl_easy_strerror(code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res;
}

} // namespace OmniPlatform
