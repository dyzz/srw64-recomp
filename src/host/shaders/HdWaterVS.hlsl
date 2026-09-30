// The water plane (battle backgrounds): the grid's model position for the waves, and its
// position in view space from the model-view rows the draw adds at float4 12.
#include "NativeMarker.hlsli"

struct Varyings {
    float4 position : SV_Position;
    float3 view : TEXCOORD0;
    float2 plane : TEXCOORD1;
};

Varyings VSMain(uint vertex : SV_VertexID) {
    const uint word = gIndices[vertex] * 9;
    const float3 p = MeshFloat3(word);
    Varyings result;
    result.position = MarkerClip(p);
    result.view = NativeRows(12, float4(p, 1)).xyz;
    result.plane = p.xz;
    return result;
}
