// The board the original name plate covers: x -100..100, y 0..30 at z 0.
#include "NativeMarker.hlsli"

struct Varyings {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Varyings VSMain(uint vertex : SV_VertexID) {
    const float2 corner = float2(vertex & 1, vertex >> 1);
    Varyings result;
    result.position = MarkerClip(float3(lerp(-100.0f, 100.0f, corner.x), 30.0f * corner.y, 0));
    result.uv = float2(corner.x, 1.0f - corner.y);
    return result;
}
