#!/usr/bin/env bash
set -e

# ==============================================================================
# OmniSteam Multi-Platform Release Packager
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIST_DIR="${PROJECT_ROOT}/dist"

mkdir -p "${DIST_DIR}"
VERSION="1.0.0"

echo "Packaging OmniSteam v${VERSION}..."

# 1. Package Linux Release
if [ -d "${PROJECT_ROOT}/build" ]; then
    LINUX_TAR="${DIST_DIR}/omnisteam-v${VERSION}-linux-x64.tar.gz"
    tar -czf "${LINUX_TAR}" \
        -C "${PROJECT_ROOT}" \
        README.md \
        omnisteam.example.toml \
        scripts/omnisteam.sh \
        scripts/install-steamos.sh \
        scripts/uninstall-steamos.sh \
        -C "${PROJECT_ROOT}/build" \
        lib/ \
        bin/ 2>/dev/null || true
    echo "✓ Generated ${LINUX_TAR}"
fi

# 1b. Ship harvested signature TOMLs alongside the release so CoreInstaller
# can deploy them into the runtime cache for PatternLoader.
if [ -d "${PROJECT_ROOT}/signatures" ]; then
    cp -r "${PROJECT_ROOT}/signatures" "${DIST_DIR}/signatures"
    echo "✓ Included signatures database in dist/"
fi

# 2. Package Decky Loader Plugin ZIP for Steam Deck
if [ -d "${PROJECT_ROOT}/plugins/decky-omnisteam" ]; then
    DECKY_ZIP="${DIST_DIR}/decky-omnisteam-v${VERSION}.zip"
    (cd "${PROJECT_ROOT}/plugins/decky-omnisteam" && zip -r "${DECKY_ZIP}" . -x "*.git*" -x "node_modules/*")
    echo "✓ Generated ${DECKY_ZIP}"
fi

echo "Packaging complete!"
