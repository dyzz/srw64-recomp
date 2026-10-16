# Linux ARMv7 (hard float), for the V0 probe host run under qemu-arm (build_probe.py --qemu).
# The CPU flags match VitaSDK's defaults (Cortex-A9 class: ARMv7-A, NEON, Thumb-2).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
# _FILE_OFFSET_BITS=64: the usual 32-bit Linux setting; without it stat() fails with EOVERFLOW on
# file systems with 64-bit inode numbers (Docker bind mounts) and the runtime cannot save.
set(CMAKE_C_FLAGS_INIT "-march=armv7-a -mfpu=neon -mfloat-abi=hard -mthumb -D_FILE_OFFSET_BITS=64")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon -mfloat-abi=hard -mthumb -D_FILE_OFFSET_BITS=64")
set(CMAKE_FIND_ROOT_PATH /usr/arm-linux-gnueabihf)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
