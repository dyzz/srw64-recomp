// Vertex-coloured ship and landmark models (indexed triangles, 7 words per vertex).
#include "NativeMarker.hlsli"

struct Varyings {
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float4 colour : COLOR0;
};

Varyings VSMain(uint vertex : SV_VertexID) {
    const uint word = gIndices[vertex] * 7;
    const uint rgba = gVertices[word + 6];
    Varyings result;
    result.position = MarkerClip(MeshFloat3(word));
    result.normal = MarkerNormal(MeshFloat3(word + 3));
    result.colour = float4(rgba & 0xFF, (rgba >> 8) & 0xFF, (rgba >> 16) & 0xFF, rgba >> 24) / 255.0f;
    return result;
}
