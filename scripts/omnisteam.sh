#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

LIB_PATH_64="${PROJECT_ROOT}/build/lib/libomnisteam.so"

if [ -f "$LIB_PATH_64" ]; then
    export LD_PRELOAD="${LIB_PATH_64}:${LD_PRELOAD}"
    echo "[OmniSteam] Preloading library: ${LIB_PATH_64}"
else
    echo "[OmniSteam] Error: libomnisteam.so not found. Please build the project first."
    exit 1
fi

exec steam "$@"
