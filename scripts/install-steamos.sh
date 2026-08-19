#!/usr/bin/env bash
set -e

# ==============================================================================
# OmniSteam SteamOS / Steam Deck One-Click Installer (Non-Root / Read-Only Safe)
# ==============================================================================

echo "========================================="
echo "🎮 Installing OmniSteam for Steam Deck..."
echo "========================================="

HOME_DIR="${HOME:-/home/deck}"
OMNI_CONFIG_DIR="${HOME_DIR}/.config/omnisteam"
OMNI_LUA_DIR="${OMNI_CONFIG_DIR}/lua"
OMNI_BIN_DIR="${HOME_DIR}/.local/share/omnisteam"

# 1. Create XDG config & runtime directories
mkdir -p "${OMNI_LUA_DIR}"
mkdir -p "${OMNI_BIN_DIR}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 2. Copy binaries and patterns
if [ -f "${PROJECT_ROOT}/build/lib/libomnisteam.so" ]; then
    cp "${PROJECT_ROOT}/build/lib/libomnisteam.so" "${OMNI_BIN_DIR}/"
    echo "✓ Installed libomnisteam.so to ${OMNI_BIN_DIR}"
fi

if [ -f "${PROJECT_ROOT}/build/bin/omnisteam" ]; then
    cp "${PROJECT_ROOT}/build/bin/omnisteam" "${OMNI_BIN_DIR}/"
    echo "✓ Installed omnisteam to ${OMNI_BIN_DIR}"
fi

# 3. Copy default configuration if absent
if [ ! -f "${OMNI_CONFIG_DIR}/omnisteam.toml" ]; then
    if [ -f "${PROJECT_ROOT}/omnisteam.example.toml" ]; then
        cp "${PROJECT_ROOT}/omnisteam.example.toml" "${OMNI_CONFIG_DIR}/omnisteam.toml"
        echo "✓ Created default configuration at ${OMNI_CONFIG_DIR}/omnisteam.toml"
    fi
fi

# 4. Integrate into Steam launch environment (User Level)
ENV_FILE="${HOME_DIR}/.config/environment.d/omnisteam.conf"
mkdir -p "$(dirname "${ENV_FILE}")"
cat << EOF > "${ENV_FILE}"
LD_PRELOAD=${OMNI_BIN_DIR}/libomnisteam.so:\${LD_PRELOAD}
EOF

echo "✓ Added systemd user environment hook at ${ENV_FILE}"

# 5. Setup Decky plugin if Decky Loader is present
DECKY_PLUGINS_DIR="${HOME_DIR}/homebrew/plugins"
if [ -d "${DECKY_PLUGINS_DIR}" ]; then
    mkdir -p "${DECKY_PLUGINS_DIR}/decky-omnisteam"
    cp -r "${PROJECT_ROOT}/plugins/decky-omnisteam/"* "${DECKY_PLUGINS_DIR}/decky-omnisteam/"
    echo "✓ Installed Decky Loader plugin into ${DECKY_PLUGINS_DIR}/decky-omnisteam"
fi

echo ""
echo "============================================================"
echo "🎉 OmniSteam successfully installed on SteamOS / Steam Deck!"
echo "👉 Restart Steam or reboot your Steam Deck to activate."
echo "============================================================"
