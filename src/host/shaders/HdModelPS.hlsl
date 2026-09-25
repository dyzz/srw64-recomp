// Colours are authored in display (sRGB) values, like the N64 frame buffer.
#include "NativeMarker.hlsli"

float4 PSMain(float4 position : SV_Position, float3 normal : NORMAL, float4 colour : COLOR0) : SV_Target {
    const float3 base = colour.rgb;
    if (colour.a < .5f) return float4(saturate(base * 1.3f + .08f), 1);  // engine glow
    const float3 n = normalize(normal);
    const float3 view = float3(0, 0, 1);
    const float3 key = normalize(float3(-.5f, .8f, .7f));
    const float3 fill = normalize(float3(.7f, -.1f, .5f));
    const float diffuse = max(dot(n, key), 0.0f);
    const float spec = pow(max(dot(n, normalize(key + view)), 0.0f), 36.0f);
    const float rim = pow(1.0f - abs(dot(n, view)), 3.0f);
    const float3 c = base * (.36f + .64f * diffuse + .15f * max(dot(n, fill), 0.0f)) + spec * .2f + base * rim * .1f;
    return float4(saturate(c), 1);
}
