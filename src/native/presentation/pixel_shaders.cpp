#include "pixel_compositor.hpp"
#include <stdexcept>
#if defined(__APPLE__)
#include "PixelVS.hlsl.metal.h"
#include "PixelPS.hlsl.metal.h"
#else
#include "PixelVS.hlsl.spirv.h"
#include "PixelPS.hlsl.spirv.h"
#ifdef _WIN32
#include "PixelVS.hlsl.dxil.h"
#include "PixelPS.hlsl.dxil.h"
#endif
#endif
namespace srw64::presentation {
namespace {
// RT64's file_to_c declares const char arrays. View the exact bytes without a
// string conversion (shader binaries contain NUL) or signed-char element cast.
template<size_t N> std::span<const unsigned char> bytes(const char (&value)[N]) {
    return {reinterpret_cast<const unsigned char*>(value), N};
}
}
PixelShaders embedded_pixel_shaders(plume::RenderShaderFormat format) {
#if defined(__APPLE__)
    if (format == plume::RenderShaderFormat::METAL) return {bytes(PixelVSBlobMSL), bytes(PixelPSBlobMSL), format};
#else
    if (format == plume::RenderShaderFormat::SPIRV) return {bytes(PixelVSBlobSPIRV), bytes(PixelPSBlobSPIRV), format};
#ifdef _WIN32
    if (format == plume::RenderShaderFormat::DXIL) return {bytes(PixelVSBlobDXIL), bytes(PixelPSBlobDXIL), format};
#endif
#endif
    throw std::runtime_error("This build has no pixel compositor shaders for the requested backend");
}
}
