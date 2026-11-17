#!/usr/bin/env bash
# OmniSteam macOS injection launcher (DYLD_INSERT_LIBRARIES).
#
# Usage: scripts/omnisteam-macos.sh [steam args...]
#
# Prefers the build-tree universal dylib, falls back to the copy installed by
# the Manager into the Steam data directory. Execs steam_osx directly because
# `open -a Steam` does not forward DYLD_* environment variables.
#
# Caveats (pending real-machine verification, ROADMAP P0):
# - SIP strips DYLD_* for platform binaries / library-validation executables;
#   Valve's Steam.app is developer-signed, so insertion is expected to work,
#   but hardened-runtime flags must be confirmed on a real Mac.
# - On Apple Silicon the Steam client runs under Rosetta 2 (x86_64); the
#   universal dylib's x86_64 slice is what actually loads.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

STEAM_DATA="${HOME}/Library/Application Support/Steam"
CANDIDATES=(
    "${PROJECT_ROOT}/build/lib/libomnisteam.dylib"
    "${STEAM_DATA}/libomnisteam.dylib"
)

LIB_PATH=""
for candidate in "${CANDIDATES[@]}"; do
    if [ -f "$candidate" ]; then
        LIB_PATH="$candidate"
        break
    fi
done

if [ -z "$LIB_PATH" ]; then
    echo "[OmniSteam] Error: libomnisteam.dylib not found (build the project or install the core via the Manager)." >&2
    exit 1
fi

STEAM_BIN="/Applications/Steam.app/Contents/MacOS/steam_osx"
if [ ! -x "$STEAM_BIN" ]; then
    STEAM_BIN="${HOME}/Applications/Steam.app/Contents/MacOS/steam_osx"
fi
if [ ! -x "$STEAM_BIN" ]; then
    echo "[OmniSteam] Error: Steam.app not found in /Applications or ~/Applications." >&2
    exit 1
fi

if pgrep -x steam_osx >/dev/null 2>&1; then
    echo "[OmniSteam] Steam is already running; quit it first so the injection takes effect." >&2
    exit 1
fi

export DYLD_INSERT_LIBRARIES="${LIB_PATH}${DYLD_INSERT_LIBRARIES:+:${DYLD_INSERT_LIBRARIES}}"
echo "[OmniSteam] Inserting library: ${LIB_PATH}"
exec "$STEAM_BIN" "$@"