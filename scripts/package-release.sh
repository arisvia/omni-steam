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
        patterns/ \
        scripts/omnisteam.sh \
        scripts/install-steamos.sh \
        scripts/uninstall-steamos.sh \
        -C "${PROJECT_ROOT}/build" \
        lib/ \
        bin/
    echo "✓ Generated ${LINUX_TAR}"
fi

echo "Packaging complete!"
