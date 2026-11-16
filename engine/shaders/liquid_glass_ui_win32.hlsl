// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0



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
