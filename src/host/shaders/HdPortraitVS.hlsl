// Whole HD portraits (native_portrait.cpp): one quad in full-frame N64 pixels.
// Data: rect, uv (a flipped portrait swaps u), resolution, fill.
#include "NativeGpu.hlsli"

struct Varyings {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Varyings VSMain(uint vertex : SV_VertexID) {
    const float2 corner = float2(vertex & 1, vertex >> 1);
    const float4 rect = NativeData(0), uv = NativeData(1);
    Varyings result;
    result.position = float4(NativeFrame(lerp(rect.xy, rect.zw, corner), NativeData(2)), 0, 1);
    result.uv = lerp(uv.xy, uv.zw, corner);
    return result;
}
