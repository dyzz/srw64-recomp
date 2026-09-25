// The portrait is premultiplied. The silhouette palette paints every opaque texel
// one dark grey (data 3: rgb, and w > 0 for a silhouette).
#include "NativeGpu.hlsli"

Texture2D<float4> gImage : register(t0, space1);
SamplerState gSampler : register(s1, space1);

float4 PSMain(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    const float4 c = gImage.Sample(gSampler, uv);
    const float4 fill = NativeData(3);
    return fill.w > 0 ? float4(fill.rgb * c.a, c.a) : c;
}
