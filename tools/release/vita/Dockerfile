# PS Vita cross-compilation environment (docs/design/psv-port.md, V0): the official
# VitaSDK image (arm-vita-eabi-gcc, vitaGL, SDL2, FreeType, HarfBuzz, ICU in
# /usr/local/vitasdk) plus the Linux ARMv7 toolchain and qemu-arm that the V0 probe
# runs the recompiled game under for its correctness check (build_probe.py --qemu).
FROM vitasdk/vitasdk:latest
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
        qemu-user time \
    && rm -rf /var/lib/apt/lists/*
