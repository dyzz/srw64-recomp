// Scene sprites and native text (native_sprite.cpp). Each draw's data: mvp rows 0-3,
// RT64's viewport scale 4 and translate 5, resolution 6, screen scale/offset 7,
// then rect 8, uv 9, colour (tint, prim alpha) 10, z 11.
// flags bit 1: a quad, rect (left, top, right, bottom) in the sprite's model space,
// y up, transformed like RT64 draws the original vertices; otherwise a texture
// rectangle, rect in full-frame N64 screen pixels.
#include "NativeGpu.hlsli"

struct Varyings {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Varyings VSMain(uint vertex : SV_VertexID) {
    const float2 corner = float2(vertex & 1, vertex >> 1);
    const float4 rect = NativeData(8), uv = NativeData(9), resolution = NativeData(6);
    Varyings result;
    result.uv = lerp(uv.xy, uv.zw, corner);
    if ((gParams.flags & 2) == 0) {
        result.position = float4(NativeFrame(lerp(rect.xy, rect.zw, corner), resolution), 0, 1);
        return result;
    }
    const float3 model = float3(lerp(rect.x, rect.z, corner.x), lerp(rect.y, rect.w, corner.y), NativeData(11).x);
    const float4 p = NativeData(0) * model.x + NativeData(1) * model.y + NativeData(2) * model.z + NativeData(3);
    const float4 scale = NativeData(4), translate = NativeData(5), screen = NativeData(7);
    const float3 window = p.xyz / float3(p.w, -p.w, p.w) * scale.xyz + translate.xyz;
    const float2 clip = NativeClip(window.xy, resolution.xy) * screen.xy + screen.zw;
    result.position = float4(clip * p.w, window.z * p.w, p.w);
    return result;
}
