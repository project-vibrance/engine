// Separable Gaussian blur for the live DWM backdrop path.
// A dense, normalised Gaussian kernel is prepared once per radius on the CPU.
// Adjacent weights share a bilinear fetch; only tails whose combined weight
// is below 1e-7 are omitted. The maximum radius retains all 61 source taps.
// The first pass consumes the sharp live source; the second consumes its
// horizontal Gaussian result. Neither consumes Acrylic or Mica output.
Texture2D backdrop : register(t0);
SamplerState backdropSampler : register(s0);

cbuffer BlurParameters : register(b0)
{
    float2 texelSize;
    float centreWeight;
    uint pairCount;
    float4 pairs[15];
};

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 blur_axis(float2 uv, float2 axis)
{
    float4 colour = backdrop.Sample(backdropSampler, uv) * centreWeight;
    float2 stepUv = axis * texelSize;
    [loop]
    for (uint pair = 0; pair < pairCount; ++pair)
    {
        float2 offset = stepUv * pairs[pair].x;
        colour += backdrop.SampleLevel(backdropSampler, uv + offset, 0.0) * pairs[pair].y;
        colour += backdrop.SampleLevel(backdropSampler, uv - offset, 0.0) * pairs[pair].y;
    }
    return colour;
}

float4 blur_horizontal(PixelInput input) : SV_Target
{
    return blur_axis(input.uv, float2(1.0, 0.0));
}

float4 blur_vertical(PixelInput input) : SV_Target
{
    return blur_axis(input.uv, float2(0.0, 1.0));
}
