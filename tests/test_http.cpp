#include "omni_check.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

#include "OmniPlatform/OmniPlatform.h"

// Offline contract tests for OmniPlatform::Http. Everything here has to pass with no
// outbound network: GitHub's i386/macOS/Windows runners and restricted CI egress make
// real endpoint assertions flaky, and the whole point of the checks below is that a
// bad URL or an unreachable host fails *before* anything leaves the process.
namespace {
using Clock = std::chrono::steady_clock;

int64_t ElapsedMs(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

// A closed port on loopback: connect() is refused immediately on every platform, and
// nothing can be listening there because binding a reserved port needs root.
constexpr const char* kRefusedUrl = "http://127.0.0.1:1/omnisteam-probe";
// RFC 1122 reserved, non-routable: connect() either black-holes until the timeout or
// fails fast with "network unreachable". Both outcomes must still respect the budget.
constexpr const char* kBlackholeUrl = "http://10.255.255.1/omnisteam-probe";

void TestEmptyUrlRejected() {
    const auto refused = OmniPlatform::Http::Get("", 1500);
    OMNI_CHECK(refused.statusCode == 0);
    OMNI_CHECK(!refused.error.empty());
    OMNI_CHECK(refused.body.empty());

    const auto refusedPost = OmniPlatform::Http::Post("", "{}", "application/json", 1500);
    OMNI_CHECK(refusedPost.statusCode == 0);
    OMNI_CHECK(!refusedPost.error.empty());
    std::cout << "[PASS] TestEmptyUrlRejected\n";
}

void TestUnsupportedSchemeRejected() {
    // Both backends keep a literal http/https allow-list (CURLOPT_PROTOCOLS on Posix,
    // scheme check after WinHttpCrackUrl on Windows); anything else must fail cleanly.
    const char* badUrls[] = {
        "file:///etc/passwd",
        "file://C:/Windows/win.ini",
        "ftp://127.0.0.1/pub",
        "gopher://gopher.example/1",
    };
    for (const char* url : badUrls) {
        const auto res = OmniPlatform::Http::Get(url, 1500);
        OMNI_CHECK(res.statusCode == 0);
        OMNI_CHECK(!res.error.empty());
        OMNI_CHECK(res.body.empty());

        const auto post = OmniPlatform::Http::Post(url, "payload", "text/plain", 1500);
        OMNI_CHECK(post.statusCode == 0);
        OMNI_CHECK(!post.error.empty());
    }
    std::cout << "[PASS] TestUnsupportedSchemeRejected\n";
}

void TestConnectionRefusedFailsFast() {
    const auto start = Clock::now();
    const auto res = OmniPlatform::Http::Get(kRefusedUrl, 3000);
    const int64_t elapsed = ElapsedMs(start);

    OMNI_CHECK(res.statusCode == 0);
    OMNI_CHECK(!res.error.empty());
    OMNI_CHECK(res.body.empty());
    // Guards against a lost timeout/handle leak turning the suite into a 30s stall.
    // Loopback refusals return immediately, but WinHTTP may still spend time on proxy
    // auto-detection first, so the bound is generous rather than tight.
    OMNI_CHECK(elapsed < 6000);
    std::cout << "[PASS] TestConnectionRefusedFailsFast (" << elapsed << "ms)\n";
}

void TestZeroTimeoutDoesNotHang() {
    // timeoutMs <= 0 means "do not install an explicit timeout" on both backends, so a
    // refused connection still has to return through the transport's own failure path.
    const auto start = Clock::now();
    const auto res = OmniPlatform::Http::Get(kRefusedUrl, 0);
    const int64_t elapsed = ElapsedMs(start);

    OMNI_CHECK(res.statusCode == 0);
    OMNI_CHECK(!res.error.empty());
    OMNI_CHECK(elapsed < 8000);
    std::cout << "[PASS] TestZeroTimeoutDoesNotHang (" << elapsed << "ms)\n";
}

void TestTimeoutBoundsRequest() {
    const auto start = Clock::now();
    const auto res = OmniPlatform::Http::Get(kBlackholeUrl, 1000);
    const int64_t elapsed = ElapsedMs(start);

    OMNI_CHECK(res.statusCode == 0);
    OMNI_CHECK(!res.error.empty());
    // curl honours CONNECTTIMEOUT_MS, WinHttpSetTimeouts covers resolve+connect+send+recv
    // separately, so a single budget multiplies on Windows. The bound only has to catch a
    // request that ignores the timeout entirely.
    OMNI_CHECK(elapsed < 10000);
    std::cout << "[PASS] TestTimeoutBoundsRequest (" << elapsed << "ms, " << res.error << ")\n";
}

void TestInsecureTlsOptInRoundTrip() {
    const bool initial = OmniPlatform::Http::IsInsecureTlsAllowed();

    OmniPlatform::Http::SetAllowInsecureTls(true);
    OMNI_CHECK(OmniPlatform::Http::IsInsecureTlsAllowed());

    OmniPlatform::Http::SetAllowInsecureTls(false);
    OMNI_CHECK(!OmniPlatform::Http::IsInsecureTlsAllowed());

    // Enabling the bypass must not change how a plain HTTP failure is reported.
    OmniPlatform::Http::SetAllowInsecureTls(true);
    const auto res = OmniPlatform::Http::Get(kRefusedUrl, 2000);
    OMNI_CHECK(res.statusCode == 0);
    OMNI_CHECK(!res.error.empty());
    OmniPlatform::Http::SetAllowInsecureTls(initial);
    OMNI_CHECK(OmniPlatform::Http::IsInsecureTlsAllowed() == initial);
    std::cout << "[PASS] TestInsecureTlsOptInRoundTrip\n";
}

void TestRequestCodesStayWithinBudget() {
    // The core calls Http::Get with 4s/5s defaults; make sure an explicit small budget
    // is actually applied instead of the backend's own (much larger) default.
    const auto start = Clock::now();
    const auto res = OmniPlatform::Http::Get(kBlackholeUrl, 250);
    const int64_t elapsed = ElapsedMs(start);

    OMNI_CHECK(res.statusCode == 0);
    OMNI_CHECK(!res.error.empty());
    OMNI_CHECK(elapsed < 10000);
    std::cout << "[PASS] TestRequestCodesStayWithinBudget (" << elapsed << "ms)\n";
}
} // namespace

int main() {
    std::cout << "Running OmniSteam HTTP transport tests...\n";
    TestEmptyUrlRejected();
    TestUnsupportedSchemeRejected();
    TestConnectionRefusedFailsFast();
    TestZeroTimeoutDoesNotHang();
    TestTimeoutBoundsRequest();
    TestInsecureTlsOptInRoundTrip();
    TestRequestCodesStayWithinBudget();
    std::cout << "All HTTP transport tests passed!\n";
    return 0;
}
