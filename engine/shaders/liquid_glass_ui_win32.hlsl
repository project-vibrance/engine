// The Vulkan island UI is copied into a shared DX11 texture and composed
// after the DWM lens. Its premultiplied alpha keeps text and controls sharp.
Texture2D islandUi : register(t0);
SamplerState islandSampler : register(s0);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target
{
    return islandUi.Sample(islandSampler, input.uv);
}
