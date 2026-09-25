// The world-map marker, ring, ship models, trail and name plates (native_marker.cpp).
// Each draw's data starts with its transform: mvp rows 0-3, normal-view rows 4-7,
// then RT64's viewport scale and translate, the target resolution and the screen
// scale/offset; extras follow from float4 12.
#include "NativeGpu.hlsli"

static const float kPi = 3.14159265f;

// The same as the original Metal code's row-vector transform (float4x4 * float4 there).
float4 NativeRows(uint first, float4 value) {
    return NativeData(first) * value.x + NativeData(first + 1) * value.y + NativeData(first + 2) * value.z + NativeData(first + 3) * value.w;
}

// Model space to clip space through the game's own viewport, like RT64 draws it.
float4 MarkerClip(float3 position) {
    const float4 p = NativeRows(0, float4(position, 1));
    const float4 scale = NativeData(8), translate = NativeData(9), resolution = NativeData(10), screen = NativeData(11);
    const float3 window = p.xyz / float3(p.w, -p.w, p.w) * scale.xyz + translate.xyz;
    float2 clip = NativeClip(window.xy, resolution.xy);
    clip = clip * screen.xy + screen.zw;
    return float4(clip * p.w, window.z * p.w, p.w);
}

float3 MarkerNormal(float3 normal) {
    return NativeRows(4, float4(normal, 0)).xyz;
}

// Mesh vertices as 32-bit words: position, normal, then (models) an RGBA8 colour.
StructuredBuffer<uint> gVertices : register(t0, space1);
StructuredBuffer<uint> gIndices : register(t1, space1);

float3 MeshFloat3(uint word) {
    return asfloat(uint3(gVertices[word], gVertices[word + 1], gVertices[word + 2]));
}
