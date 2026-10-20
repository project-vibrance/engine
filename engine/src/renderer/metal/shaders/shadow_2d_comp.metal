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
uint vibrance_primitive_type(constant Renderer2DConstants& pc)
{
    return pc.data.x & 255u;
}

static inline __attribute__((always_inline))
float notch_flare_size(thread const float2& size, thread const float& amount, thread const float& depth)
{
    float progress = fast::clamp((fast::clamp(amount, 0.0, 1.0) - 0.85000002384185791015625) / 0.1500000059604644775390625, 0.0, 1.0);
    float _515;
    if (depth > 0.5)
    {
        _515 = depth;
    }
    else
    {
        _515 = fast::clamp(size.y * 0.62000000476837158203125, 0.0, 92.0);
    }
    float depthTarget = _515;
    float flareTarget = fast::clamp(depthTarget * 0.180000007152557373046875, 0.0, size.y * 0.5);
    float sizeRatio = fast::clamp((size.y * 0.5) / fast::max(flareTarget, 0.001000000047497451305389404296875), 0.0, 1.0);
    float flareT = sizeRatio * progress;
    return (flareTarget * flareT) * flareT;
}

static inline __attribute__((always_inline))
float primitive_bounds_padding(constant Renderer2DConstants& pc)
{
    if (vibrance_primitive_type(pc) != 7u)
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
    bool _360 = outside.x == 0.0;
    bool _367;
    if (!_360)
    {
        _367 = outside.y == 0.0;
    }
    else
    {
        _367 = _360;
    }
    float _368;
    if (_367)
    {
        _368 = outside.x + outside.y;
    }
    else
    {
        _368 = powr(powr(outside.x, cornerPower) + powr(outside.y, cornerPower), 1.0 / cornerPower);
    }
    float squircleNorm = _368;
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
    float _837;
    if (arcParams.w >= 0.0)
    {
        float param = angle - arcParams.z;
        _837 = positive_angle(param);
    }
    else
    {
        float param_1 = arcParams.z - angle;
        _837 = positive_angle(param_1);
    }
    float relativeAngle = _837;
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
        float _265;
        if (p.x < 0.0)
        {
            _265 = radii.x;
        }
        else
        {
            _265 = radii.y;
        }
        return _265;
    }
    float _278;
    if (p.x < 0.0)
    {
        _278 = radii.w;
    }
    else
    {
        _278 = radii.z;
    }
    return _278;
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
    bool _457 = outside.x == 0.0;
    bool _464;
    if (!_457)
    {
        _464 = outside.y == 0.0;
    }
    else
    {
        _464 = _457;
    }
    float _465;
    if (_464)
    {
        _465 = outside.x + outside.y;
    }
    else
    {
        _465 = powr(powr(outside.x, cornerPower) + powr(outside.y, cornerPower), 1.0 / cornerPower);
    }
    float squircleNorm = _465;
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
    if (vibrance_primitive_type(pc) == 8u)
    {
        float2 param = pixel;
        float4 param_1 = rect;
        float4 param_2 = pc.uvRect;
        return sd_circular_progress(param, param_1, param_2);
    }
    if (vibrance_primitive_type(pc) == 3u)
    {
        float2 q = p / halfSize;
        return (length(q) - 1.0) * fast::min(halfSize.x, halfSize.y);
    }
    float _958;
    if (vibrance_primitive_type(pc) == 1u)
    {
        _958 = 0.0;
    }
    else
    {
        _958 = fast::clamp(cornerRadius, 0.0, fast::min(halfSize.x, halfSize.y));
    }
    float radius = _958;
    float squircleAmount = fast::clamp(pc.effect1.x, 0.0, 1.0);
    float _980;
    if (pc.effect1.y <= 0.001000000047497451305389404296875)
    {
        _980 = 4.0;
    }
    else
    {
        _980 = pc.effect1.y;
    }
    float squirclePower = fast::clamp(_980, 2.0, 5.0);
    if (vibrance_primitive_type(pc) == 6u)
    {
        float2 param_3 = p;
        float2 param_4 = halfSize;
        float param_5 = radius;
        float param_6 = squircleAmount;
        float param_7 = squirclePower;
        return sd_squircle_box(param_3, param_4, param_5, param_6, param_7);
    }
    if (vibrance_primitive_type(pc) == 7u)
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
    bool _1030 = vibrance_primitive_type(pc) != 1u;
    bool _1038;
    if (_1030)
    {
        _1038 = (pc.data.y & 65536u) != 0u;
    }
    else
    {
        _1038 = _1030;
    }
    if (_1038)
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
    bool _1062 = (pc.data.y & 16777216u) != 0u;
    bool _1067;
    if (_1062)
    {
        _1067 = vibrance_primitive_type(pc) == 7u;
    }
    else
    {
        _1067 = _1062;
    }
    if (_1067)
    {
        float2 halfSize = fast::max(pc.rect.zw * 0.5, float2(0.5));
        float2 p = pixel - (pc.rect.xy + halfSize);
        float radius = fast::clamp(pc.effect0.x, 0.0, fast::min(halfSize.x, halfSize.y));
        float _1101;
        if (pc.effect1.y <= 0.001000000047497451305389404296875)
        {
            _1101 = 4.0;
        }
        else
        {
            _1101 = pc.effect1.y;
        }
        float2 param = p;
        float2 param_1 = halfSize;
        float param_2 = radius;
        float param_3 = fast::clamp(pc.effect1.x, 0.0, 1.0);
        float param_4 = fast::clamp(_1101, 2.0, 5.0);
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

kernel void shadow_2d_comp(constant Renderer2DConstants& pc [[buffer(0)]], texture2d<float, access::read_write> colorBuffer [[texture(0)]], uint3 gl_NumWorkGroups [[threadgroups_per_grid]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    uint tail = pc.data.x >> 8u;
    uint2 exactExtent = (gl_NumWorkGroups.xy * uint2(8u)) - uint2(tail & 7u, (tail >> 3u) & 7u);
    if (any(gl_GlobalInvocationID.xy >= exactExtent))
    {
        return;
    }
    int2 pixel = dispatch_origin(pc) + int2(gl_GlobalInvocationID.xy);
    int2 screenSize = int2(colorBuffer.get_width(), colorBuffer.get_height());
    bool _1203 = pixel.x < 0;
    bool _1210;
    if (!_1203)
    {
        _1210 = pixel.y < 0;
    }
    else
    {
        _1210 = _1203;
    }
    bool _1219;
    if (!_1210)
    {
        _1219 = pixel.x >= screenSize.x;
    }
    else
    {
        _1219 = _1210;
    }
    bool _1228;
    if (!_1219)
    {
        _1228 = pixel.y >= screenSize.y;
    }
    else
    {
        _1228 = _1219;
    }
    if (_1228)
    {
        return;
    }
    float2 p = float2(pixel) + float2(0.5);
    float blurRadius = fast::max(pc.effect0.y, 0.5);
    float boundsPad = blurRadius + primitive_bounds_padding(pc);
    bool _1250 = p.x < (pc.rect.x - boundsPad);
    bool _1261;
    if (!_1250)
    {
        _1261 = p.y < (pc.rect.y - boundsPad);
    }
    else
    {
        _1261 = _1250;
    }
    bool _1275;
    if (!_1261)
    {
        _1275 = p.x > ((pc.rect.x + pc.rect.z) + boundsPad);
    }
    else
    {
        _1275 = _1261;
    }
    bool _1289;
    if (!_1275)
    {
        _1289 = p.y > ((pc.rect.y + pc.rect.w) + boundsPad);
    }
    else
    {
        _1289 = _1275;
    }
    if (_1289)
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
