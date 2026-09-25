// Dashes keep the original texture's layout: centres at 20.2 + 30k degrees measured
// as atan2(-z, x), radius 0.87-0.955 of the half size, about 10.5 degrees long; they
// turn slowly. Beneath them a dark backdrop; a ripple leaves the centre every 2.4 s
// and fades just past the dashes, and a brighter one marks the arrival.
// Data 12: beacon time, age, scale, bob.
#include "NativeMarker.hlsli"

float4 Over(float4 dst, float3 rgb, float a) {  // straight-alpha "over"
    const float result = a + dst.a * (1.0f - a);
    return result > 0.0f ? float4((rgb * a + dst.rgb * dst.a * (1.0f - a)) / result, result) : float4(0, 0, 0, 0);
}

float Band(float d, float aa) {
    return 1.0f - smoothstep(-aa, aa, d);
}

float4 PSMain(float4 position : SV_Position, float2 local : TEXCOORD0) : SV_Target {
    const float4 beacon = NativeData(12);
    const float time = beacon.x, age = beacon.y, scale = beacon.z;
    const float2 p = local / max(scale, .01f);
    const float r = length(p);
    const float aa = max(fwidth(r), 1e-3f);
    float4 c = float4(0, 0, 0, 0);
    c = Over(c, float3(.05f, .04f, 0), .22f * Band(r - 8.5f, 2.0f));
    const float cycle = frac(time / 2.4f);
    c = Over(c, float3(1.0f, .9f, .25f), .5f * pow(1.0f - cycle, 1.5f) * Band(abs(r - (5.0f + 10.0f * cycle)) - .4f, aa));
    if (age < .8f) {
        const float burst = age / .8f;
        c = Over(c, float3(1.0f, .92f, .35f), .8f * (1.0f - burst) * Band(abs(r - (4.0f + 12.0f * burst)) - .6f, aa));
    }
    const float period = 2.0f * kPi / 12.0f;
    float phase = atan2(-p.y, p.x) - (20.2f + 18.0f * time) * kPi / 180.0f;
    phase -= round(phase / period) * period;
    const float centre = 12.8f, halfWidth = .6f, halfLength = 5.25f * kPi / 180.0f * centre;
    const float along = max(abs(phase * r) - (halfLength - halfWidth), 0.0f);
    const float dist = length(float2(along, r - centre)) - halfWidth;  // rounded arc
    const float daa = max(fwidth(dist), 1e-3f);
    c = Over(c, float3(.28f, .2f, 0), .45f * Band(dist - .3f, daa));  // faint dark edge on bright land
    c = Over(c, float3(1.0f, .9f, .12f), Band(dist, daa));
    if (c.a <= 0.0f) discard;
    return c;
}
