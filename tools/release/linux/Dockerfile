# Linux x64 build environment for the game host (docs/guide/linux-build.md).
# Ubuntu 22.04 fixes the glibc 2.35 baseline; RT64's prebuilt dxc-linux needs 2.34.
# Runtime libraries (SDL, FreeType, HarfBuzz, ICU) are built from the pinned
# sources by build_linux.py and shipped beside the program, not taken from here.
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git xz-utils pkg-config make ninja-build cmake \
        clang lld python3.11 python3.11-venv \
        zlib1g-dev libdbus-1-dev libudev-dev \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
        libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols \
        libdecor-0-dev libegl1-mesa-dev libgl-dev libdrm-dev libgbm-dev \
        libasound2-dev libpulse-dev libpipewire-0.3-dev libibus-1.0-dev \
    && rm -rf /var/lib/apt/lists/*
# Packaging: point the bundled libraries at each other ($ORIGIN) and inspect them.
RUN apt-get update && apt-get install -y --no-install-recommends patchelf binutils file \
    && rm -rf /var/lib/apt/lists/*
