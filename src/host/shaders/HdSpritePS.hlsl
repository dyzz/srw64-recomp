// Premultiplied images, tinted, times the prim alpha. flags bit 0: repeat across
// (scrolling strips); otherwise clamped. Bit 2: the depth pass, the opaque half of the
// picture only and no colour (native_sprite.cpp draw_sprite).
#include "NativeGpu.hlsli"

Texture2D<float4> gImage : register(t0, space1);
SamplerState gClamp : register(s1, space1);
SamplerState gWrap : register(s2, space1);

float4 PSMain(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    const float4 c = (gParams.flags & 1) ? gImage.Sample(gWrap, uv) : gImage.Sample(gClamp, uv);
    const float4 colour = NativeData(10);
    if (gParams.flags & 4) {
        if (c.a * colour.a < 0.5) discard;
        return float4(0, 0, 0, 0);
    }
    return float4(c.rgb * colour.rgb, c.a) * colour.a;
}
