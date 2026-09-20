// No vertex buffers, transforms, UV interpolation or platform coordinate math.
float4 VSMain(uint index : SV_VertexID) : SV_Position {
    const float2 positions[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    return float4(positions[index], 0, 1);
}
