#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

template <typename ImageT>
void spvImageFence(ImageT img) { img.fence(); }

struct Hosted3DCompositeConstants
{
    uint4 dispatchRect;
};

static inline __attribute__((always_inline))
int2 scale_pixel_to_image(thread const int2& pixel, thread const int2& dstSize, thread const int2& srcSize)
{
    float2 uv = (float2(pixel) + float2(0.5)) / fast::max(float2(dstSize), float2(1.0));
    return clamp(int2(uv * float2(srcSize)), int2(0), srcSize - int2(1));
}

static inline __attribute__((always_inline))
float4 blend_premultiplied_over(thread const float4& dst, thread const float4& src)
{
    float srcA = fast::clamp(src.w, 0.0, 1.0);
    float outA = srcA + (dst.w * (1.0 - srcA));
    float3 outRgbPremultiplied = src.xyz + ((dst.xyz * dst.w) * (1.0 - srcA));
    float3 _59;
    if (outA > 9.9999997473787516355514526367188e-05)
    {
        _59 = outRgbPremultiplied / float3(outA);
    }
    else
    {
        _59 = float3(0.0);
    }
    float3 outRgb = _59;
    return float4(outRgb, outA);
}

kernel void hosted_3d_composite_comp(constant Hosted3DCompositeConstants& pc [[buffer(0)]], texture2d<float, access::read_write> colorBuffer [[texture(0)]], texture2d<float> modelColorBuffer [[texture(1)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    bool _119 = gl_GlobalInvocationID.x >= pc.dispatchRect.z;
    bool _129;
    if (!_119)
    {
        _129 = gl_GlobalInvocationID.y >= pc.dispatchRect.w;
    }
    else
    {
        _129 = _119;
    }
    if (_129)
    {
        return;
    }
    int2 pixel = int2(pc.dispatchRect.xy) + int2(gl_GlobalInvocationID.xy);
    int2 screenSize = int2(colorBuffer.get_width(), colorBuffer.get_height());
    bool _153 = pixel.x < 0;
    bool _160;
    if (!_153)
    {
        _160 = pixel.y < 0;
    }
    else
    {
        _160 = _153;
    }
    bool _169;
    if (!_160)
    {
        _169 = pixel.x >= screenSize.x;
    }
    else
    {
        _169 = _160;
    }
    bool _178;
    if (!_169)
    {
        _178 = pixel.y >= screenSize.y;
    }
    else
    {
        _178 = _169;
    }
    if (_178)
    {
        return;
    }
    int2 modelSize = int2(modelColorBuffer.get_width(), modelColorBuffer.get_height());
    int2 param = pixel;
    int2 param_1 = screenSize;
    int2 param_2 = modelSize;
    float4 modelColor = modelColorBuffer.read(uint2(scale_pixel_to_image(param, param_1, param_2)));
    if (modelColor.w <= 0.001000000047497451305389404296875)
    {
        return;
    }
    spvImageFence(colorBuffer);
    float4 sceneColor = colorBuffer.read(uint2(pixel));
    float4 param_3 = sceneColor;
    float4 param_4 = modelColor;
    colorBuffer.write(blend_premultiplied_over(param_3, param_4), uint2(pixel));
}

