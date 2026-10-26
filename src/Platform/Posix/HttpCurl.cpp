#include <atomic>
#include <curl/curl.h>
#include <limits>
#include <memory>
#include <mutex>
#include <string>

#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {
namespace {
std::atomic<bool> g_httpAllowInsecureTls{false};
std::once_flag g_curlInitFlag;
CURLcode g_curlInitResult = CURLE_FAILED_INIT;

using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using CurlHeaders = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

bool EnsureCurlInitialized() {
    std::call_once(g_curlInitFlag, [] { g_curlInitResult = curl_global_init(CURL_GLOBAL_DEFAULT); });
    return g_curlInitResult == CURLE_OK;
}

size_t WriteResponse(void* contents, size_t size, size_t count, void* userData) {
    if (!userData || (size != 0 && count > std::numeric_limits<size_t>::max() / size))
        return 0;

    const size_t total = size * count;
    static_cast<std::string*>(userData)->append(static_cast<const char*>(contents), total);
    return total;
}

void ConfigureCommon(CURL* curl, const std::string& url, int timeoutMs, Http::Response& response) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteResponse);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, OmniEndpoints::Http::kUserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    if (timeoutMs > 0) {
        // curl reads these options as `long` through varargs; passing a bare `int` leaves the
        // high half of the register slot undefined on ABIs like macOS arm64.
        const long timeout = static_cast<long>(timeoutMs);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
    }
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    if (g_httpAllowInsecureTls.load(std::memory_order_relaxed)) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
}

bool Perform(CURL* curl, Http::Response& response) {
    char errorBuffer[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        response.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
        return false;
    }

    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    response.statusCode = static_cast<int>(statusCode);
    return true;
}
} // namespace

void Http::SetAllowInsecureTls(bool allowed) {
    g_httpAllowInsecureTls.store(allowed, std::memory_order_relaxed);
}

Http::Response Http::Get(const std::string& url, int timeoutMs) {
    Http::Response response;
    if (url.empty()) {
        response.error = "URL is empty";
        return response;
    }
    if (!EnsureCurlInitialized()) {
        response.error = "curl_global_init failed";
        return response;
    }

    CurlHandle curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }
    ConfigureCommon(curl.get(), url, timeoutMs, response);

    Perform(curl.get(), response);
    return response;
}

Http::Response Http::Post(const std::string& url, const std::string& body, const std::string& contentType,
                          int timeoutMs) {
    Http::Response response;
    if (url.empty()) {
        response.error = "URL is empty";
        return response;
    }
    if (!EnsureCurlInitialized()) {
        response.error = "curl_global_init failed";
        return response;
    }

    CurlHandle curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    CurlHeaders headers(nullptr, &curl_slist_free_all);
    curl_slist* rawHeaders = curl_slist_append(nullptr, ("Content-Type: " + contentType).c_str());
    if (!rawHeaders) {
        response.error = "Failed to allocate HTTP headers";
        return response;
    }
    headers.reset(rawHeaders);

    ConfigureCommon(curl.get(), url, timeoutMs, response);
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));

    Perform(curl.get(), response);
    return response;
}

} // namespace OmniPlatform
