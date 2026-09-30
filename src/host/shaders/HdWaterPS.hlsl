// Moving water in place of the original's textured plane. Directional waves whose
// x periods divide the model's 1000-unit width (the battle draws it three times side
// by side, so the seams meet), a Fresnel mix of deep water and the sky's gradient, the
// sun's glitter and a haze towards the horizon. Draw data from float4 12: model-view
// rows (12-15), then time in seconds (16.x) and the layer's alpha (16.y): the surface
// layer blends over what is under it (a unit standing in the water), more opaque at grazing angles.
#include "NativeMarker.hlsli"

static const uint kWaves = 16;
// (waves per 1000 units along x (whole, so the three copies meet), along z, slope)
static const float3 kWave[kWaves] = {
    float3(2, 9, .06f), float3(-7, 11, .05f), float3(16, 10, .05f), float3(-26, 8, .045f), float3(13, -36, .04f),
    float3(39, 39, .04f), float3(-19, 77, .035f), float3(111, -22, .03f), float3(-97, -123, .03f), float3(23, 221, .028f),
    float3(187, 131, .025f), float3(-251, 97, .022f), float3(61, -317, .02f), float3(349, 211, .018f),
    float3(-433, -281, .016f), float3(127, 509, .014f)};

float3 Srgb(float3 value) { return pow(saturate(value), 1.0f / 2.2f); }
float3 Linear(float3 value) { return pow(value, 2.2f); }

float4 PSMain(float4 position : SV_Position, float3 view : TEXCOORD0, float2 plane : TEXCOORD1) : SV_Target {
    const float time = NativeData(16).x;
    // slopes of the height field in the model's x and z
    float2 slope = 0;
    for (uint i = 0; i < kWaves; ++i) {
        const float2 kv = 2.0f * kPi * kWave[i].xy / 1000.0f;
        const float k = length(kv);
        const float omega = sqrt(9.8f * k * 40.0f);   // deep-water speed at this scale
        slope += kv / k * kWave[i].z * cos(dot(kv, plane) - omega * time * 0.12f);
    }
    // detail fades with distance so the far water does not shimmer
    const float distance = length(view);
    slope *= lerp(1.0f, 0.45f, saturate(distance / 1600.0f));
    const float3 up = normalize(NativeRows(4, float4(0, 1, 0, 0)).xyz);
    const float3 n = normalize(NativeRows(4, float4(-slope.x, 1.0f, -slope.y, 0)).xyz);
    const float3 v = normalize(-view);
    const float3 r = reflect(-v, n);

    const float3 zenith = Linear(float3(.28f, .46f, .78f)), horizon = Linear(float3(.62f, .74f, .90f));
    const float elevation = saturate(dot(r, up));
    const float3 sky = lerp(horizon, zenith, pow(elevation, .6f));
    const float3 deep = Linear(float3(.02f, .11f, .30f)), shallow = Linear(float3(.04f, .20f, .42f));
    const float facing = saturate(dot(n, v));
    const float fresnel = .02f + .98f * pow(1.0f - facing, 5.0f);
    float3 water = lerp(shallow, deep, saturate(facing * 1.4f));
    float3 c = lerp(water, sky, fresnel * .75f);
    // the sun low ahead, behind the city: a glitter path on the water
    const float3 sun = normalize(NativeRows(4, float4(-0.25f, 0.30f, -0.92f, 0)).xyz);
    const float glint = pow(saturate(dot(r, sun)), 2500.0f) * 5.0f + pow(saturate(dot(r, sun)), 120.0f) * .12f;
    c += glint * float3(1.0f, .95f, .85f);
    // crests catch a little white
    c += saturate(length(slope) * 2.5f - .6f) * .06f;
    // haze towards the horizon
    c = lerp(c, horizon, saturate((distance - 700.0f) / 2200.0f) * .7f);
    const float alpha = NativeData(16).y;
    return float4(Srgb(c), alpha >= 1 ? 1 : saturate(lerp(alpha, 1.0f, fresnel)));
}
