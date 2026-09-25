// The ring's plane (y = 4), a little wider than the original 14 so the ripple can
// clear the dashes; every effect stays within the original ring's footprint.
#include "NativeMarker.hlsli"

struct Varyings {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
};

Varyings VSMain(uint vertex : SV_VertexID) {
    const float2 xz = float2((vertex & 1) ? 17.0f : -17.0f, (vertex >> 1) ? 17.0f : -17.0f);
    Varyings result;
    result.position = MarkerClip(float3(xz.x, 4, xz.y));
    result.local = xz;
    return result;
}
