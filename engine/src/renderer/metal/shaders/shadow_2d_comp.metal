#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

// Implementation of the GLSL mod() function, which is slightly different than Metal fmod()
template<typename Tx, typename Ty>
inline Tx mod(Tx x, Ty y)
{
    return x - y * floor(x / y);
}

template <typename ImageT>
void spvImageFence(ImageT img) { img.fence(); }

struct Renderer2DConstants
{
    float4 rect;
    float4 uvRect;
    float4 color0;
    float4 color1;
    float4 color2;
    float4 effect0;
    float4 effect1;
    uint4 data;
};

static inline __attribute__((always_inline))
int2 dispatch_origin(constant Renderer2DConstants& pc)
{
    return int2(int(pc.data.w & 65535u), int((pc.data.w >> 16u) & 65535u));
}

static inline __attribute__((always_inline))
float notch_flare_size(thread const float2& size, thread const float& amount, thread const float& depth)
{
    float progress = fast::clamp((fast::clamp(amount, 0.0, 1.0) - 0.85000002384185791015625) / 0.1500000059604644775390625, 0.0, 1.0);
    float _507;
    if (depth > 0.5)
    {
        _507 = depth;
    }
    else
    {
        _507 = fast::clamp(size.y * 0.62000000476837158203125, 0.0, 92.0);
    }
    float depthTarget = _507;
    float flareTarget = fast::clamp(depthTarget * 0.180000007152557373046875, 0.0, size.y * 0.5);
    float sizeRatio = fast::clamp((size.y * 0.5) / fast::max(flareTarget, 0.001000000047497451305389404296875), 0.0, 1.0);
    float flareT = sizeRatio * progress;
    return (flareTarget * flareT) * flareT;
}

static inline __attribute__((always_inline))
float primitive_bounds_padding(constant Renderer2DConstants& pc)
{
    if (pc.data.x != 7u)
    {
        return 0.0;
    }
    float2 param = fast::max(pc.rect.zw, float2(1.0));
    float param_1 = pc.effect1.z;
    float param_2 = pc.effect1.w;
    return notch_flare_size(param, param_1, param_2) + 6.0;
}

static inline __attribute__((always_inline))
float sd_squircle_box(thread const float2& p, thread const float2& halfSize, thread const float& radius, thread const float& amount, thread const float& power)
{
    float safeRadius = fast::clamp(radius, 0.0, fast::min(halfSize.x, halfSize.y));
    float2 q = abs(p) - fast::max(halfSize - float2(safeRadius), float2(0.0));
    float2 outside = fast::max(q, float2(0.0));
    float roundedNorm = length(outside);
    if (amount <= 0.0)
    {
        return (roundedNorm + fast::min(fast::max(q.x, q.y), 0.0)) - safeRadius;
    }
    float cornerPower = mix(2.0, fast::clamp(power, 2.0, 5.0), fast::clamp(amount, 0.0, 1.0));
    bool _352 = outside.x == 0.0;
    bool _359;
    if (!_352)
    {
        _359 = outside.y == 0.0;
    }
    else
    {
        _359 = _352;
    }
    float _360;
    if (_359)
    {
        _360 = outside.x + outside.y;
    }
    else
    {
        _360 = powr(powr(outside.x, cornerPower) + powr(outside.y, cornerPower), 1.0 / cornerPower);
    }
    float squircleNorm = _360;
    return (mix(roundedNorm, squircleNorm, fast::clamp(amount, 0.0, 1.0)) + fast::min(fast::max(q.x, q.y), 0.0)) - safeRadius;
}

static inline __attribute__((always_inline))
float4 unpack_corner_radii(thread const uint& _packed)
{
    return float4(float(_packed & 255u), float((_packed >> 8u) & 255u), float((_packed >> 16u) & 255u), float((_packed >> 24u) & 255u));
}

static inline __attribute__((always_inline))
float positive_angle(thread const float& angle)
{
    return mod(mod(angle, 6.283185482025146484375) + 6.283185482025146484375, 6.283185482025146484375);
}

static inline __attribute__((always_inline))
float sd_circular_progress(thread const float2& pixel, thread const float4& rect, thread const float4& arcParams)
{
    float progress = fast::clamp(arcParams.x, 0.0, 1.0);
    if (progress <= 9.9999997473787516355514526367188e-06)
    {
        return 1000000.0;
    }
    float2 halfSize = fast::max(rect.zw * 0.5, float2(0.5));
    float2 p = pixel - (rect.xy + halfSize);
    float outerRadius = fast::max(fast::min(halfSize.x, halfSize.y), 0.5);
    float halfStroke = fast::clamp(arcParams.y * 0.5, 0.0, outerRadius);
    float centerRadius = fast::max(outerRadius - halfStroke, 0.0);
    float ringSdf = abs(length(p) - centerRadius) - halfStroke;
    if (progress >= 0.999989986419677734375)
    {
        return ringSdf;
    }
    float angle = precise::atan2(p.y, p.x);
    float _829;
    if (arcParams.w >= 0.0)
    {
        float param = angle - arcParams.z;
        _829 = positive_angle(param);
    }
    else
    {
        float param_1 = arcParams.z - angle;
        _829 = positive_angle(param_1);
    }
    float relativeAngle = _829;
    float sweep = 6.283185482025146484375 * progress;
    if (relativeAngle <= sweep)
    {
        return ringSdf;
    }
    float direction = (arcParams.w >= 0.0) ? 1.0 : (-1.0);
    float2 startPoint = float2(cos(arcParams.z), sin(arcParams.z)) * centerRadius;
    float endAngle = arcParams.z + (direction * sweep);
    float2 endPoint = float2(cos(endAngle), sin(endAngle)) * centerRadius;
    return fast::min(length(p - startPoint), length(p - endPoint)) - halfStroke;
}

static inline __attribute__((always_inline))
float corner_radius_for(thread const float2& p, thread const float4& radii)
{
    if (p.y < 0.0)
    {
        float _257;
        if (p.x < 0.0)
        {
            _257 = radii.x;
        }
        else
        {
            _257 = radii.y;
        }
        return _257;
    }
    float _270;
    if (p.x < 0.0)
    {
        _270 = radii.w;
    }
    else
    {
        _270 = radii.z;
    }
    return _270;
}

static inline __attribute__((always_inline))
float sd_squircle_box_corners(thread const float2& p, thread const float2& halfSize, thread const float4& radii, thread const float& amount, thread const float& power)
{
    float2 param = p;
    float4 param_1 = radii;
    float safeRadius = fast::clamp(corner_radius_for(param, param_1), 0.0, fast::min(halfSize.x, halfSize.y));
    float2 q = abs(p) - fast::max(halfSize - float2(safeRadius), float2(0.0));
    float2 outside = fast::max(q, float2(0.0));
    float roundedNorm = length(outside);
    if (amount <= 0.0)
    {
        return (roundedNorm + fast::min(fast::max(q.x, q.y), 0.0)) - safeRadius;
    }
    float cornerPower = mix(2.0, fast::clamp(power, 2.0, 5.0), fast::clamp(amount, 0.0, 1.0));
    bool _449 = outside.x == 0.0;
    bool _456;
    if (!_449)
    {
        _456 = outside.y == 0.0;
    }
    else
    {
        _456 = _449;
    }
    float _457;
    if (_456)
    {
        _457 = outside.x + outside.y;
    }
    else
    {
        _457 = powr(powr(outside.x, cornerPower) + powr(outside.y, cornerPower), 1.0 / cornerPower);
    }
    float squircleNorm = _457;
    return (mix(roundedNorm, squircleNorm, fast::clamp(amount, 0.0, 1.0)) + fast::min(fast::max(q.x, q.y), 0.0)) - safeRadius;
}

static inline __attribute__((always_inline))
float sd_box(thread const float2& p, thread const float2& halfSize)
{
    float2 q = abs(p) - halfSize;
    return length(fast::max(q, float2(0.0))) + fast::min(fast::max(q.x, q.y), 0.0);
}

static inline __attribute__((always_inline))
float sd_dynamic_notch(thread const float2& p, thread const float2& halfSize, thread const float& radius, thread const float& amount, thread const float& squircleAmount, thread const float& power, thread const float& depth)
{
    float topRadius = radius * fast::clamp(1.0 - (fast::clamp(amount, 0.0, 1.0) / 0.85000002384185791015625), 0.0, 1.0);
    float2 param = halfSize * 2.0;
    float param_1 = amount;
    float param_2 = depth;
    float flare = notch_flare_size(param, param_1, param_2);
    float bottomRadius = fast::clamp(radius, 0.0, fast::min(halfSize.x, halfSize.y));
    float2 param_3 = p;
    float2 param_4 = halfSize;
    float4 param_5 = float4(topRadius, topRadius, bottomRadius, bottomRadius);
    float param_6 = fast::max(squircleAmount, fast::clamp(amount, 0.0, 1.0));
    float param_7 = power;
    float body = sd_squircle_box_corners(param_3, param_4, param_5, param_6, param_7);
    if (flare <= 0.0500000007450580596923828125)
    {
        return body;
    }
    float2 topCenter = float2(0.0, (-halfSize.y) + (flare * 0.5));
    float2 param_8 = p - topCenter;
    float2 param_9 = float2(halfSize.x + flare, flare * 0.5);
    float topBar = sd_box(param_8, param_9);
    float leftCutout = length(p - float2((-halfSize.x) - flare, (-halfSize.y) + flare)) - flare;
    float rightCutout = length(p - float2(halfSize.x + flare, (-halfSize.y) + flare)) - flare;
    float topFlares = fast::max(topBar, fast::max(-leftCutout, -rightCutout));
    return fast::min(body, topFlares);
}

static inline __attribute__((always_inline))
float sd_notched_squircle(thread const float2& pixel, thread const float4& rect, thread const float& cornerRadius, thread const float& squircleAmount, thread const float& squirclePower, thread const float& notchAmount, thread const float& notchDepthOverride)
{
    float2 size = fast::max(rect.zw, float2(1.0));
    float amount = fast::clamp(notchAmount, 0.0, 1.0);
    float autoDepth = fast::clamp(size.y * 0.62000000476837158203125, 0.0, 92.0);
    float requestedDepth = (notchDepthOverride > 0.5) ? notchDepthOverride : autoDepth;
    if (amount <= 0.00999999977648258209228515625)
    {
        float2 halfSize = fast::max(size * 0.5, float2(0.5));
        float2 center = rect.xy + halfSize;
        float2 p = pixel - center;
        float2 param = p;
        float2 param_1 = halfSize;
        float param_2 = fast::clamp(cornerRadius, 0.0, fast::min(halfSize.x, halfSize.y));
        float param_3 = squircleAmount;
        float param_4 = squirclePower;
        return sd_squircle_box(param, param_1, param_2, param_3, param_4);
    }
    float2 halfSize_1 = fast::max(size * 0.5, float2(0.5));
    float2 center_1 = rect.xy + halfSize_1;
    float radius = fast::clamp(cornerRadius, 0.0, fast::min(halfSize_1.x, halfSize_1.y));
    float2 param_5 = pixel - center_1;
    float2 param_6 = halfSize_1;
    float param_7 = radius;
    float param_8 = amount;
    float param_9 = squircleAmount;
    float param_10 = squirclePower;
    float param_11 = requestedDepth;
    return sd_dynamic_notch(param_5, param_6, param_7, param_8, param_9, param_10, param_11);
}

static inline __attribute__((always_inline))
float sd_rounded_box(thread const float2& p, thread const float2& halfSize, thread const float& radius)
{
    float2 q = abs(p) - fast::max(halfSize - float2(radius), float2(0.0));
    return (length(fast::max(q, float2(0.0))) + fast::min(fast::max(q.x, q.y), 0.0)) - radius;
}

static inline __attribute__((always_inline))
float sd_rounded_box_corners(thread const float2& p, thread const float2& halfSize, thread const float4& radii)
{
    float2 param = p;
    float4 param_1 = radii;
    float radius = fast::clamp(corner_radius_for(param, param_1), 0.0, fast::min(halfSize.x, halfSize.y));
    float2 param_2 = p;
    float2 param_3 = halfSize;
    float param_4 = radius;
    return sd_rounded_box(param_2, param_3, param_4);
}

static inline __attribute__((always_inline))
float shadow_sdf_for(thread const float2& pixel, thread const float4& rect, thread const float& cornerRadius, thread const float4& cornerRadii, constant Renderer2DConstants& pc)
{
    float2 halfSize = fast::max(rect.zw * 0.5, float2(0.5));
    float2 center = rect.xy + halfSize;
    float2 p = pixel - center;
    if (pc.data.x == 8u)
    {
        float2 param = pixel;
        float4 param_1 = rect;
        float4 param_2 = pc.uvRect;
        return sd_circular_progress(param, param_1, param_2);
    }
    if (pc.data.x == 3u)
    {
        float2 q = p / halfSize;
        return (length(q) - 1.0) * fast::min(halfSize.x, halfSize.y);
    }
    float _953;
    if (pc.data.x == 1u)
    {
        _953 = 0.0;
    }
    else
    {
        _953 = fast::clamp(cornerRadius, 0.0, fast::min(halfSize.x, halfSize.y));
    }
    float radius = _953;
    float squircleAmount = fast::clamp(pc.effect1.x, 0.0, 1.0);
    float _975;
    if (pc.effect1.y <= 0.001000000047497451305389404296875)
    {
        _975 = 4.0;
    }
    else
    {
        _975 = pc.effect1.y;
    }
    float squirclePower = fast::clamp(_975, 2.0, 5.0);
    if (pc.data.x == 6u)
    {
        float2 param_3 = p;
        float2 param_4 = halfSize;
        float param_5 = radius;
        float param_6 = squircleAmount;
        float param_7 = squirclePower;
        return sd_squircle_box(param_3, param_4, param_5, param_6, param_7);
    }
    if (pc.data.x == 7u)
    {
        float2 param_8 = pixel;
        float4 param_9 = rect;
        float param_10 = radius;
        float param_11 = squircleAmount;
        float param_12 = squirclePower;
        float param_13 = pc.effect1.z;
        float param_14 = pc.effect1.w;
        return sd_notched_squircle(param_8, param_9, param_10, param_11, param_12, param_13, param_14);
    }
    bool _1028 = pc.data.x != 1u;
    bool _1036;
    if (_1028)
    {
        _1036 = (pc.data.y & 65536u) != 0u;
    }
    else
    {
        _1036 = _1028;
    }
    if (_1036)
    {
        float2 param_15 = p;
        float2 param_16 = halfSize;
        float4 param_17 = cornerRadii;
        return sd_rounded_box_corners(param_15, param_16, param_17);
    }
    float2 param_18 = p;
    float2 param_19 = halfSize;
    float param_20 = radius;
    return sd_rounded_box(param_18, param_19, param_20);
}

static inline __attribute__((always_inline))
float shadow_sdf(thread const float2& pixel, constant Renderer2DConstants& pc)
{
    bool _1060 = (pc.data.y & 16777216u) != 0u;
    bool _1066;
    if (_1060)
    {
        _1066 = pc.data.x == 7u;
    }
    else
    {
        _1066 = _1060;
    }
    if (_1066)
    {
        float2 halfSize = fast::max(pc.rect.zw * 0.5, float2(0.5));
        float2 p = pixel - (pc.rect.xy + halfSize);
        float radius = fast::clamp(pc.effect0.x, 0.0, fast::min(halfSize.x, halfSize.y));
        float _1100;
        if (pc.effect1.y <= 0.001000000047497451305389404296875)
        {
            _1100 = 4.0;
        }
        else
        {
            _1100 = pc.effect1.y;
        }
        float2 param = p;
        float2 param_1 = halfSize;
        float param_2 = radius;
        float param_3 = fast::clamp(pc.effect1.x, 0.0, 1.0);
        float param_4 = fast::clamp(_1100, 2.0, 5.0);
        return sd_squircle_box(param, param_1, param_2, param_3, param_4);
    }
    uint param_5 = pc.data.z;
    float2 param_6 = pixel;
    float4 param_7 = pc.rect;
    float param_8 = pc.effect0.x;
    float4 param_9 = unpack_corner_radii(param_5);
    return shadow_sdf_for(param_6, param_7, param_8, param_9, pc);
}

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

kernel void shadow_2d_comp(constant Renderer2DConstants& pc [[buffer(0)]], texture2d<float, access::read_write> colorBuffer [[texture(0)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    int2 pixel = dispatch_origin(pc) + int2(gl_GlobalInvocationID.xy);
    int2 screenSize = int2(colorBuffer.get_width(), colorBuffer.get_height());
    bool _1176 = pixel.x < 0;
    bool _1183;
    if (!_1176)
    {
        _1183 = pixel.y < 0;
    }
    else
    {
        _1183 = _1176;
    }
    bool _1192;
    if (!_1183)
    {
        _1192 = pixel.x >= screenSize.x;
    }
    else
    {
        _1192 = _1183;
    }
    bool _1201;
    if (!_1192)
    {
        _1201 = pixel.y >= screenSize.y;
    }
    else
    {
        _1201 = _1192;
    }
    if (_1201)
    {
        return;
    }
    float2 p = float2(pixel) + float2(0.5);
    float blurRadius = fast::max(pc.effect0.y, 0.5);
    float boundsPad = blurRadius + primitive_bounds_padding(pc);
    bool _1223 = p.x < (pc.rect.x - boundsPad);
    bool _1234;
    if (!_1223)
    {
        _1234 = p.y < (pc.rect.y - boundsPad);
    }
    else
    {
        _1234 = _1223;
    }
    bool _1248;
    if (!_1234)
    {
        _1248 = p.x > ((pc.rect.x + pc.rect.z) + boundsPad);
    }
    else
    {
        _1248 = _1234;
    }
    bool _1262;
    if (!_1248)
    {
        _1262 = p.y > ((pc.rect.y + pc.rect.w) + boundsPad);
    }
    else
    {
        _1262 = _1248;
    }
    if (_1262)
    {
        return;
    }
    float2 param = p;
    float sdf = shadow_sdf(param, pc);
    float alpha = ((1.0 - smoothstep(-blurRadius, blurRadius, sdf)) * pc.color0.w) * pc.effect0.w;
    if ((pc.data.y & 8388608u) != 0u)
    {
        float sourceCornerRadius = fast::max(pc.effect0.x - pc.effect0.z, 0.0);
        float2 param_1 = p;
        float4 param_2 = pc.color1;
        float param_3 = sourceCornerRadius;
        float4 param_4 = pc.color2;
        float sourceSdf = shadow_sdf_for(param_1, param_2, param_3, param_4, pc);
        float clipFeather = fast::clamp(blurRadius * 0.07999999821186065673828125, 0.75, 2.0);
        alpha *= smoothstep(-clipFeather, clipFeather, sourceSdf);
    }
    if (alpha <= 0.001000000047497451305389404296875)
    {
        return;
    }
    float4 shadow = float4(pc.color0.xyz, alpha);
    spvImageFence(colorBuffer);
    float4 dst = colorBuffer.read(uint2(pixel));
    float4 param_5 = dst;
    float4 param_6 = shadow;
    colorBuffer.write(blend_over(param_5, param_6), uint2(pixel));
}
