#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct Composite2DConstants
{
    uint4 data;
};

static inline __attribute__((always_inline))
float4 blend_over(thread const float4& dst, thread const float4& src)
{
    float outA = src.w + (dst.w * (1.0 - src.w));
    float3 outRgb = (src.xyz * src.w) + ((dst.xyz * dst.w) * (1.0 - src.w));
    if (outA > 9.9999997473787516355514526367188e-05)
    {
        outRgb /= float3(outA);
    }
    return float4(outRgb, outA);
}

kernel void composite_2d_comp(constant Composite2DConstants& pc [[buffer(0)]], texture2d<float> sceneColorBuffer [[texture(0)]], texture2d<float> uiStaticSurface [[texture(1)]], texture2d<float> uiExternalBackdropSurface [[texture(2)]], texture2d<float, access::write> tempSurface [[texture(3)]], texture2d<float, access::write> compositionSurface [[texture(4)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    int2 pixel = int2(gl_GlobalInvocationID.xy + pc.data.yz);
    int2 screenSize = int2(sceneColorBuffer.get_width(), sceneColorBuffer.get_height());
    bool _95 = pixel.x < 0;
    bool _103;
    if (!_95)
    {
        _103 = pixel.y < 0;
    }
    else
    {
        _103 = _95;
    }
    bool _112;
    if (!_103)
    {
        _112 = pixel.x >= screenSize.x;
    }
    else
    {
        _112 = _103;
    }
    bool _121;
    if (!_112)
    {
        _121 = pixel.y >= screenSize.y;
    }
    else
    {
        _121 = _112;
    }
    if (_121)
    {
        return;
    }
    float4 sceneColor = sceneColorBuffer.read(uint2(pixel));
    float4 color;
    if ((pc.data.x & 8u) != 0u)
    {
        // The Metal renderer replays both UI layers into sceneColorBuffer.
        // Its static surface is blank, so avoid a texture read and two
        // straight-alpha blends across every drawable pixel.
        color = (pc.data.x & 1u) != 0u ?
            blend_over(uiExternalBackdropSurface.read(uint2(pixel)), sceneColor) :
            sceneColor;
    }
    else
    {
        float4 staticColor = uiStaticSurface.read(uint2(pixel));
        color = float4(0.0);
        if ((pc.data.x & 1u) != 0u)
            color = uiExternalBackdropSurface.read(uint2(pixel));
        color = blend_over(color, staticColor);
        color = blend_over(color, sceneColor);
    }
    if ((pc.data.x & 4u) != 0u)
    {
        tempSurface.write(color, uint2(pixel));
    }
    if ((pc.data.x & 2u) != 0u)
    {
        compositionSurface.write(float4(color.xyz * color.w, color.w), uint2(pixel));
    }
}
