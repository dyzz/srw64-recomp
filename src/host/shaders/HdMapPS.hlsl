// The HD map follows the palette the game has loaded now (cycles, day/night swaps):
// the base was composed with the reference palette, so each pixel moves by the
// difference between the live and reference colours of its original index.
// Data from float4 4: the reference palette, then the live one (256 RGBA8 words each).
// flags bit 0: the base is premultiplied and drawn translucent (window frames).
#include "NativeGpu.hlsli"

Texture2D<float4> gBase : register(t0, space1);
Texture2D<uint> gIndex : register(t1, space1);
SamplerState gSampler : register(s2, space1);

float3 Unpack(uint colour) {
    return float3(colour & 0xFF, (colour >> 8) & 0xFF, (colour >> 16) & 0xFF) / 255.0f;
}

float4 PSMain(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    const float4 painted = gBase.Sample(gSampler, uv);
    uint width, height;
    gIndex.GetDimensions(width, height);
    const uint2 texel = min(uint2(uv * float2(width, height)), uint2(width, height) - 1);
    const uint index = gIndex.Load(int3(texel, 0));
    const float3 shift = Unpack(NativeWord(68, index)) - Unpack(NativeWord(4, index));
    if ((gParams.flags & 1) == 0) return float4(saturate(painted.rgb + shift), 1);
    const float3 straight = painted.a > 0 ? painted.rgb / painted.a : 0;
    return float4(saturate(straight + shift) * painted.a, painted.a);
}
