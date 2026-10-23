# Toolchain check for the Switch port (docs/design/switch-port.md, S0): SDL2 window, an
# EGL context from devkitPro's Mesa, and the facts S0 needs printed over nxlink.
cmake_minimum_required(VERSION 3.20)
project(srw64-smoke CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_executable(srw64-smoke main.cpp)
target_compile_features(srw64-smoke PRIVATE cxx_std_20)
target_link_libraries(srw64-smoke PRIVATE PkgConfig::SDL2)

nx_generate_nacp(srw64-smoke.nacp NAME "Marchwind64 smoke" AUTHOR "Marchwind64" VERSION "0.0.1")
nx_create_nro(srw64-smoke NACP srw64-smoke.nacp)
