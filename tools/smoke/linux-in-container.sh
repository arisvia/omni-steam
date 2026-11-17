#!/usr/bin/env bash
# Run inside the omnisteam-linux-smoke container (repo mounted at /work).
# Builds the core, fetches current signatures, launches Steam under Xvfb with
# LD_PRELOAD injection and gates the result with tools/smoke_status.py.
set -e
cd /work

export OMNISTEAM_CACHE_DIR=/work/smoke-cache
mkdir -p "$OMNISTEAM_CACHE_DIR"

if [ ! -d "$OMNISTEAM_CACHE_DIR/signatures" ]; then
    git fetch origin data
    git archive origin/data signatures | tar -x -C "$OMNISTEAM_CACHE_DIR"
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -G Ninja
cmake --build build --target omnisteam_core

LIB=$(find build -name 'libomnisteam.so' | head -1)
[ -n "$LIB" ] || { echo "libomnisteam.so not built"; exit 1; }

export LD_PRELOAD="$PWD/$LIB"
echo "[smoke] injecting $LD_PRELOAD"
xvfb-run -a steam 2>&1 | tee steam-console.log &
STEAM_PID=$!

for i in $(seq 1 60); do
    sleep 5
    [ -f "$OMNISTEAM_CACHE_DIR/core_status.json" ] && break
done
if [ ! -f "$OMNISTEAM_CACHE_DIR/core_status.json" ]; then
    echo "[smoke] FAIL: core_status.json never appeared"
    kill $STEAM_PID 2>/dev/null || true
    exit 1
fi
sleep 10
python3 tools/smoke_status.py "$OMNISTEAM_CACHE_DIR/core_status.json" --platform linux --max-age 120
RC=$?
kill $STEAM_PID 2>/dev/null || true
pkill -x steam 2>/dev/null || true
exit $RC