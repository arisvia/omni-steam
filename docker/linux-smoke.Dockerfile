# Local Linux real-machine smoke environment (ROADMAP P0.1 substitute).
# Docker Desktop (WSL2) runs a genuine Linux kernel, so LD_PRELOAD injection,
# funchook and /proc probing behave natively. For SteamRT container-penetration
# tests, base this on registry.gitlab.steamos.cloud/steamrt/sniper/sdk instead.
#
# Build:  docker build -f docker/linux-smoke.Dockerfile -t omnisteam-linux-smoke .
# Run:    docker run --rm -it -v "%CD%":/work omnisteam-linux-smoke
#         (inside) bash tools/smoke/linux-in-container.sh
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
# The :i386 packages exist because Valve's own launcher bootstraps through the
# 32-bit ubuntu12_32/steam binary before handing over to the 64-bit steamrt64
# host. OmniSteam is 64-bit only and never reads a 32-bit client image.
RUN dpkg --add-architecture i386 && apt-get update && apt-get install -y \
    build-essential cmake ninja-build git python3 curl ca-certificates \
    libssl-dev libcurl4-openssl-dev pkg-config xvfb xauth \
    lib32gcc-s1 lib32stdc++6 libc6:i386 libgl1:i386 libx11-6:i386 \
    libxext6:i386 libxrandr2:i386 libxinerama1:i386 libxi6:i386 \
    libxcursor1:i386 libxss1:i386 libdbus-1-3:i386 libfreetype6:i386 \
    libfontconfig1:i386 libnm0:i386 sudo && \
    rm -rf /var/lib/apt/lists/*

# Steam launcher (self-updates the client on first run, same as CI harvest).
RUN curl -fsSL -o /tmp/steam-launcher.deb https://repo.steampowered.com/steam/archive/precise/steam_latest.deb && \
    (dpkg -i /tmp/steam-launcher.deb || apt-get -f install -y) && \
    rm /tmp/steam-launcher.deb

WORKDIR /work
CMD ["/bin/bash"]