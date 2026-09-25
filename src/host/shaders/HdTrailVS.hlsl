// Two vertices per trail step: a strip through the step centres on the map plane,
// as wide as the original squares plus a faint halo, extended half a square at both
// ends. Data 12: half width, point count, halo, depth pull; points (x, y, z, c) from 13.
#include "NativeMarker.hlsli"

struct Varyings {
    float4 position : SV_Position;
    float across : TEXCOORD0;
    float c : TEXCOORD1;
};

Varyings VSMain(uint vertex : SV_VertexID) {
    const float4 k = NativeData(12);
    const uint n = uint(k.y);
    const uint i = min(vertex / 2, n - 1);
    const float side = (vertex & 1) ? 1.0f : -1.0f;
    const float4 here = NativeData(13 + i);
    float2 d = NativeData(13 + min(i + 1, n - 1)).xz - NativeData(13 + (i > 0 ? i - 1 : 0)).xz;
    d = length(d) > 1e-4f ? normalize(d) : float2(1, 0);
    const float2 across = float2(-d.y, d.x) * k.x * k.z * side;
    float2 xz = here.xz + across;
    if (i == 0) xz -= d * k.x;
    if (i == n - 1) xz += d * k.x;
    float4 p = MarkerClip(float3(xz.x, here.y, xz.y));
    p.z -= k.w * p.w;  // stay in front of the map plane it lies on
    Varyings result;
    result.position = p;
    result.across = side * k.z;
    result.c = here.w;
    return result;
}
