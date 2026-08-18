#!/usr/bin/env bash
set -e

# ==============================================================================
# OmniSteam SteamOS / Steam Deck One-Click Uninstaller
# ==============================================================================

echo "========================================="
echo "🗑️  Uninstalling OmniSteam..."
echo "========================================="

HOME_DIR="${HOME:-/home/deck}"
OMNI_BIN_DIR="${HOME_DIR}/.local/share/omnisteam"
ENV_FILE="${HOME_DIR}/.config/environment.d/omnisteam.conf"
DECKY_PLUGIN_DIR="${HOME_DIR}/homebrew/plugins/decky-omnisteam"

# 1. Remove environment preload
if [ -f "${ENV_FILE}" ]; then
    rm -f "${ENV_FILE}"
    echo "✓ Removed environment hook ${ENV_FILE}"
fi

# 2. Remove Decky Plugin
if [ -d "${DECKY_PLUGIN_DIR}" ]; then
    rm -rf "${DECKY_PLUGIN_DIR}"
    echo "✓ Removed Decky plugin ${DECKY_PLUGIN_DIR}"
fi

# 3. Remove binaries
if [ -d "${OMNI_BIN_DIR}" ]; then
    rm -rf "${OMNI_BIN_DIR}"
    echo "✓ Removed binaries in ${OMNI_BIN_DIR}"
fi

echo ""
echo "Note: Your unlock scripts and configs in ~/.config/omnisteam were preserved."
echo "To completely erase them, run: rm -rf ~/.config/omnisteam"
echo "========================================="
echo "🎉 OmniSteam uninstalled successfully."
echo "========================================="
