// The baked atlas as it is (display values: sun, sky, shadows and bounce light are in
// it); glass adds what baking cannot hold, a sheen that follows the view: the sky at
// grazing angles and a highlight of the same key light the ship models use.
#include "NativeMarker.hlsli"

Texture2D<float4> gAtlas : register(t2, space1);
SamplerState gSampler : register(s3, space1);

float4 PSMain(float4 position : SV_Position, float3 normal : NORMAL, float2 uv : TEXCOORD0,
              float material : TEXCOORD1) : SV_Target {
    float3 c = gAtlas.Sample(gSampler, uv).rgb;
    if (material < .9f) {
        const float3 n = normalize(normal);
        const float3 view = float3(0, 0, 1);
        const float3 key = normalize(float3(-.5f, .8f, .7f));
        const float fresnel = .04f + .96f * pow(1.0f - saturate(abs(dot(n, view))), 5.0f);
        const float3 sky = float3(.72f, .82f, .95f);
        const float spec = pow(saturate(dot(n, normalize(key + view))), 90.0f);
        c = lerp(c, sky, saturate(fresnel * .8f)) + spec * .35f;
    }
    return float4(saturate(c), 1);
}
