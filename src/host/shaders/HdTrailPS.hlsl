// 0 at the centre, 1 at the original edge, the halo width at the halo's edge.
#include "NativeMarker.hlsli"

float4 PSMain(float4 position : SV_Position, float across : TEXCOORD0, float c : TEXCOORD1) : SV_Target {
    const float halo_width = NativeData(12).z;
    const float a = abs(across);
    const float aa = max(fwidth(across), 1e-3f);
    const float core = 1.0f - smoothstep(1.0f - aa, 1.0f + aa, a);
    const float halo = (1.0f - smoothstep(1.0f, halo_width, a)) * .3f;
    float3 colour = float3(c, c, 1.0f);  // the original prim colour (c, c, 255)
    colour = lerp(colour, float3(1, 1, 1), (1.0f - a) * (1.0f - a) * .22f * core);
    return float4(colour, max(core, halo));
}
