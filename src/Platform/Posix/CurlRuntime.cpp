#include "Platform/Posix/CurlRuntime.h"

#include <curl/curl.h>
#include <mutex>

namespace OmniPlatform::CurlRuntime {
namespace {
std::once_flag g_initFlag;
CURLcode g_initResult = CURLE_FAILED_INIT;
} // namespace

bool EnsureInitialized() {
    std::call_once(g_initFlag, [] { g_initResult = curl_global_init(CURL_GLOBAL_DEFAULT); });
    return g_initResult == CURLE_OK;
}

} // namespace OmniPlatform::CurlRuntime
