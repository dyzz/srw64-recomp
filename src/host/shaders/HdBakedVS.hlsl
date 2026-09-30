// Models with their lighting baked into an atlas (battle backgrounds): position, normal,
// uv and an RGBA8 word per vertex (9 words), the colour's alpha marking glass.
#include "NativeMarker.hlsli"

struct Varyings {
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float material : TEXCOORD1;
};

Varyings VSMain(uint vertex : SV_VertexID) {
    const uint word = gIndices[vertex] * 9;
    Varyings result;
    result.position = MarkerClip(MeshFloat3(word));
    result.normal = MarkerNormal(MeshFloat3(word + 3));
    result.uv = asfloat(uint2(gVertices[word + 6], gVertices[word + 7]));
    result.material = float(gVertices[word + 8] >> 24) / 255.0f;
    return result;
}
