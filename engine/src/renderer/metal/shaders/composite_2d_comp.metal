#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct Composite2DConstants
{
    uint4 data;
    float4 fadeRect;
    float4 fadeParameters;
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
    float4 staticColor = float4(0.0);
    if ((pc.data.x & 8u) == 0u)
    {
        staticColor = uiStaticSurface.read(uint2(pixel));
    }
    float4 color = float4(0.0);
    if ((pc.data.x & 1u) != 0u)
    {
        color = uiExternalBackdropSurface.read(uint2(pixel));
    }
    if ((pc.data.x & 8u) != 0u)
    {
        float4 param = color;
        float4 param_1 = sceneColor;
        color = blend_over(param, param_1);
    }
    else
    {
        float4 param_2 = color;
        float4 param_3 = staticColor;
        color = blend_over(param_2, param_3);
        float4 param_4 = color;
        float4 param_5 = sceneColor;
        color = blend_over(param_4, param_5);
    }
    if ((pc.data.x & 16u) != 0u)
    {
        float2 point = float2(pixel) + float2(0.5);
        float alpha = 1.0;
        bool _199 = point.x < pc.fadeRect.x;
        bool _212;
        if (!_199)
        {
            _212 = point.x >= (pc.fadeRect.x + pc.fadeRect.z);
        }
        else
        {
            _212 = _199;
        }
        bool _221;
        if (!_212)
        {
            _221 = point.y < pc.fadeRect.y;
        }
        else
        {
            _221 = _212;
        }
        bool _233;
        if (!_221)
        {
            _233 = point.y >= (pc.fadeRect.y + pc.fadeRect.w);
        }
        else
        {
            _233 = _221;
        }
        if (_233)
        {
            alpha = 0.0;
        }
        else
        {
            if (pc.fadeParameters.x > 0.0)
            {
                alpha = fast::min(alpha, smoothstep(0.0, pc.fadeParameters.x, point.y - pc.fadeRect.y));
            }
            if (pc.fadeParameters.y > 0.0)
            {
                alpha = fast::min(alpha, smoothstep(0.0, pc.fadeParameters.y, (pc.fadeRect.y + pc.fadeRect.w) - point.y));
            }
            alpha = mix(pc.fadeParameters.z, 1.0, alpha);
        }
        color.w *= alpha;
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
