// The plate in the reading language; the board is opaque.
#include "NativeGpu.hlsli"

Texture2D<float4> gPlate : register(t0, space1);
SamplerState gSampler : register(s1, space1);

float4 PSMain(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return float4(gPlate.Sample(gSampler, uv).rgb, 1);
}
