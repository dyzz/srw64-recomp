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
PixelShaders embedded_pixel_shaders(plume::RenderShaderFormat format) {
#if defined(__APPLE__)
    if (format == plume::RenderShaderFormat::METAL) return {PixelVSBlobMSL, PixelPSBlobMSL, format};
#else
    if (format == plume::RenderShaderFormat::SPIRV) return {PixelVSBlobSPIRV, PixelPSBlobSPIRV, format};
#ifdef _WIN32
    if (format == plume::RenderShaderFormat::DXIL) return {PixelVSBlobDXIL, PixelPSBlobDXIL, format};
#endif
#endif
    throw std::runtime_error("This build has no pixel compositor shaders for the requested backend");
}
}
