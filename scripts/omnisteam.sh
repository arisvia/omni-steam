#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

STEAM_DATA="${HOME}/.local/share/Steam"
CANDIDATES=(
    "${PROJECT_ROOT}/build/lib/libomnisteam.so"
    "${STEAM_DATA}/ubuntu12_64/libomnisteam.so"
)

LIB_PATH_64=""
for candidate in "${CANDIDATES[@]}"; do
    if [ -f "$candidate" ]; then
        LIB_PATH_64="$candidate"
        break
    fi
done

if [ -n "$LIB_PATH_64" ]; then
    export LD_PRELOAD="${LIB_PATH_64}:${LD_PRELOAD}"
    echo "[OmniSteam] Preloading library: ${LIB_PATH_64}"
else
    echo "[OmniSteam] Error: libomnisteam.so not found. Build the project or install the core via the Manager."
    exit 1
fi

exec steam "$@"
