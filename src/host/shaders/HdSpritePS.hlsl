// Premultiplied images, tinted, times the prim alpha. flags bit 0: repeat across
// (scrolling strips); otherwise clamped.
#include "NativeGpu.hlsli"

Texture2D<float4> gImage : register(t0, space1);
SamplerState gClamp : register(s1, space1);
SamplerState gWrap : register(s2, space1);

float4 PSMain(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    const float4 c = (gParams.flags & 1) ? gImage.Sample(gWrap, uv) : gImage.Sample(gClamp, uv);
    const float4 colour = NativeData(10);
    return float4(c.rgb * colour.rgb, c.a) * colour.a;
}
