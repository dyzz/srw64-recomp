// The golden 5600 marker (indexed triangles, 6 words per vertex).
#include "NativeMarker.hlsli"

struct Varyings {
    float4 position : SV_Position;
    float3 normal : NORMAL;
};

Varyings VSMain(uint vertex : SV_VertexID) {
    const uint word = gIndices[vertex] * 6;
    Varyings result;
    result.position = MarkerClip(MeshFloat3(word));
    result.normal = MarkerNormal(MeshFloat3(word + 3));
    return result;
}
