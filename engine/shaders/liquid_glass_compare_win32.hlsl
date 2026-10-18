// Detect any changed input texel without returning the backdrop to the CPU.
// Surviving samples make the occlusion predicate true, enabling both Gaussian
// passes. Exact loads include alpha and preserve HDR/sRGB sampling semantics.
Texture2D currentBackdrop : register(t0);
Texture2D previousBackdrop : register(t1);

void main(float4 position : SV_Position)
{
    int3 texel = int3(int2(position.xy), 0);
    float4 current = currentBackdrop.Load(texel);
    float4 previous = previousBackdrop.Load(texel);
    // Treat signed zero as a change and never reuse a NaN neighbourhood.
    if (all(asuint(current) == asuint(previous)) && all(current == current))
        discard;
}
