// The picture is premultiplied; the original combiner is TEXEL0 * PRIM (fades).
#include "NativeGpu.hlsli"

Texture2D<float4> gImage : register(t0, space1);
SamplerState gSampler : register(s1, space1);

float4 PSMain(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    const float4 colour = gImage.Sample(gSampler, uv);
    const float4 prim = NativeData(1);
    return float4(colour.rgb * prim.rgb * prim.a, colour.a * prim.a);
}
