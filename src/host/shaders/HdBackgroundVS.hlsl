// Whole HD backgrounds (native_background.cpp): one instance per original tile quad.
// Data: resolution, PRIM colour, then per quad its rect (N64 pixels) and uv (picture).
#include "NativeGpu.hlsli"

struct Varyings {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Varyings VSMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    const float4 resolution = NativeData(0);
    const float4 rect = NativeData(2 + instance * 2), uv = NativeData(3 + instance * 2);
    const float2 corner = float2(vertex & 1, vertex >> 1);
    Varyings result;
    result.position = float4(NativeFrame(lerp(rect.xy, rect.zw, corner), resolution), 0, 1);
    result.uv = lerp(uv.xy, uv.zw, corner);
    return result;
}
