Texture2D<float4> pixels : register(t0, space0);
// Both the CPU buffer and fragment position are top-down. Integer loads keep
// the old exact-pixel composition: no filtering, sRGB conversion or alpha fixup.
float4 PSMain(float4 position : SV_Position) : SV_Target {
    return pixels.Load(int3(uint2(position.xy), 0));
}
