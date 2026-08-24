#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace Manager {

// Verifies an HTTP-downloaded payload against an optional "<url>.sha256"
// sidecar published next to it.
//
// Semantics: missing or malformed sidecar -> allow (protection targets
// corruption / stale mirrors, not active adversaries who control both files).
// A published digest that mismatches the payload -> reject.
inline bool VerifyDownloadChecksumIfPublished(const std::string& url, const std::string& payload,
                                              std::string* rejectionReason = nullptr) {
    if (rejectionReason)
        rejectionReason->clear();

    auto sidecar = OmniPlatform::Http::Get(url + ".sha256", 5000);
    if (sidecar.statusCode != 200 || sidecar.body.empty()) {
        return true;
    }

    std::string expected = sidecar.body.substr(0, sidecar.body.find_first_of(" \t\r\n"));
    if (expected.size() != 64 ||
        !std::all_of(expected.begin(), expected.end(), [](unsigned char c) { return std::isxdigit(c); })) {
        spdlog::debug("DownloadVerifier: Malformed checksum sidecar for {}, skipping verification", url);
        return true;
    }
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::vector<uint8_t> bytes(payload.begin(), payload.end());
    std::string actual = OmniPlatform::Hash::Sha256(bytes);
    std::transform(actual.begin(), actual.end(), actual.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (actual == expected)
        return true;

    spdlog::warn("DownloadVerifier: SHA256 mismatch for {} (expected {}, got {})", url, expected, actual);
    if (rejectionReason)
        *rejectionReason = "SHA256 mismatch";
    return false;
}

} // namespace Manager
