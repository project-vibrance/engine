// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0




Texture2D currentBackdrop : register(t0);
Texture2D previousBackdrop : register(t1);

void main(float4 position : SV_Position)
{
    int3 texel = int3(int2(position.xy), 0);
    float4 current = currentBackdrop.Load(texel);
    float4 previous = previousBackdrop.Load(texel);

    if (all(asuint(current) == asuint(previous)) && all(current == current))
        discard;
}
