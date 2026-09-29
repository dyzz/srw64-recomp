// The HD tactical map (native_map.cpp): one quad covering the drawn map area.
// Data: rect (N64 pixels), uv (normalised map), resolution, screen scale/offset.
#include "NativeGpu.hlsli"

struct Varyings {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Varyings VSMain(uint vertex : SV_VertexID) {
    const float4 rect = NativeData(0), uv = NativeData(1), resolution = NativeData(2), screen = NativeData(3);
    const float2 corner = float2(vertex & 1, vertex >> 1);
    const float2 clip = NativeFrame(lerp(rect.xy, rect.zw, corner), resolution) * screen.xy + screen.zw;
    Varyings result;
    result.position = float4(clip, 0, 1);
    result.uv = lerp(uv.xy, uv.zw, corner);
    return result;
}
