// Bindings shared by the host-drawn HD layers (src/host/native_gpu.hpp). Set 0: the
// push constants (b0) and the per-draw data ring (t1). Set 1 (space1): each program's
// own textures and samplers, numbered without collisions across types (Vulkan puts
// every descriptor type of a set in one binding space).
struct NativeParams {
    uint data;      // first float4 of this draw in gData
    uint flags;     // program-specific
    uint2 extra;
};
[[vk::push_constant]] ConstantBuffer<NativeParams> gParams : register(b0);
StructuredBuffer<float4> gData : register(t1);

float4 NativeData(uint index) {
    return gData[gParams.data + index];
}

// The index-th 32-bit word of a table stored from float4 `base` on.
uint NativeWord(uint base, uint index) {
    const uint4 words = asuint(gData[gParams.data + base + index / 4]);
    return words[index % 4];
}

// N64 screen pixels (y down) to clip space for a target of `resolution` pixels.
float2 NativeClip(float2 pixel, float2 resolution) {
    return (pixel - resolution * 0.5f) / (resolution * float2(0.5f, -0.5f));
}
