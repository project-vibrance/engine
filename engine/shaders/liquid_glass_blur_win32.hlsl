// Separable Gaussian blur for the live DWM backdrop path.
// A 61-tap kernel samples every source texel within thirty pixels of the
// centre. Adjacent weights share a bilinear fetch, preserving the dense kernel
// without the gaps produced by scaling a small fixed set of sample offsets.
// The first pass consumes the sharp live source; the second consumes its
// horizontal Gaussian result. Neither consumes Acrylic or Mica output.
Texture2D backdrop : register(t0);
SamplerState backdropSampler : register(s0);

cbuffer BlurParameters : register(b0)
{
    float2 texelSize;
    float blurRadius;
    float unused;
};

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 blur_axis(float2 uv, float2 axis)
{
    float4 colour = backdrop.Sample(backdropSampler, uv);
    if (blurRadius > 0.0)
    {
        float sigma = clamp(blurRadius / 3.0, 0.35, 10.0);
        float inverseVariance = 0.5 / (sigma * sigma);
        float2 stepUv = axis * texelSize;
        float totalWeight = 1.0;
        [unroll]
        for (int offset = 1; offset < 30; offset += 2)
        {
            float firstWeight = exp(-float(offset * offset) * inverseVariance);
            float secondWeight =
                exp(-float((offset + 1) * (offset + 1)) * inverseVariance);
            float weight = firstWeight + secondWeight;
            float pairedOffset = float(offset) + secondWeight / max(weight, 1e-30);
            colour += backdrop.Sample(backdropSampler, uv + stepUv * pairedOffset) * weight;
            colour += backdrop.Sample(backdropSampler, uv - stepUv * pairedOffset) * weight;
            totalWeight += 2.0 * weight;
        }
        colour /= totalWeight;
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
