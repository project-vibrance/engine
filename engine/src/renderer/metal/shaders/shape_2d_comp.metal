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

struct SurfacePoint
{
    short inside;
    float2 uv;
};

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
    float _943;
    if (depth > 0.5)
    {
        _943 = depth;
    }
    else
    {
        _943 = fast::clamp(size.y * 0.62000000476837158203125, 0.0, 92.0);
    }
    float depthTarget = _943;
    float flareTarget = fast::clamp(depthTarget * 0.180000007152557373046875, 0.0, size.y * 0.5);
    float sizeRatio = fast::clamp((size.y * 0.5) / fast::max(flareTarget, 0.001000000047497451305389404296875), 0.0, 1.0);
    float flareT = sizeRatio * progress;
    return (flareTarget * flareT) * flareT;
}

static inline __attribute__((always_inline))
float primitive_bounds_padding(thread const float4& shapeParams, constant Renderer2DConstants& pc)
{
    if (pc.data.x != 7u)
    {
        return 0.0;
    }
    float2 param = fast::max(pc.rect.zw, float2(1.0));
    float param_1 = shapeParams.z;
    float param_2 = shapeParams.w;
    return notch_flare_size(param, param_1, param_2) + 6.0;
}

static inline __attribute__((always_inline))
SurfacePoint make_surface_point(thread const bool& inside, thread const float2& uv)
{
    SurfacePoint point;
    point.inside = short(inside);
    point.uv = uv;
    return point;
}

static inline __attribute__((always_inline))
float3 project_surface_corner(thread const float2& uv, constant Renderer2DConstants& pc)
{
    float3 p = float3((uv - float2(0.5)) * pc.rect.zw, 0.0);
    float pitchCos = cos(pc.effect1.y);
    float pitchSin = sin(pc.effect1.y);
    p = float3(p.x, (p.y * pitchCos) - (p.z * pitchSin), (p.y * pitchSin) + (p.z * pitchCos));
    float yawCos = cos(pc.effect1.z);
    float yawSin = sin(pc.effect1.z);
    p = float3((p.x * yawCos) + (p.z * yawSin), p.y, ((-p.x) * yawSin) + (p.z * yawCos));
    float perspective = fast::max(pc.effect1.w, 0.0);
    float q = 1.0;
    if (perspective > 0.001000000047497451305389404296875)
    {
        float cameraDistance = fast::max(fast::min(pc.rect.z, pc.rect.w), 1.0) / perspective;
        q = cameraDistance / fast::max(cameraDistance + p.z, 1.0);
    }
    float2 projected = p.xy * q;
    float zCos = cos(pc.effect1.x);
    float zSin = sin(pc.effect1.x);
    projected = float2((projected.x * zCos) - (projected.y * zSin), (projected.x * zSin) + (projected.y * zCos));
    return float3((pc.rect.xy + (pc.rect.zw * 0.5)) + projected, q);
}

static inline __attribute__((always_inline))
float3 barycentric_coords(thread const float2& p, thread const float2& a, thread const float2& b, thread const float2& c)
{
    float2 v0 = b - a;
    float2 v1 = c - a;
    float2 v2 = p - a;
    float denom = (v0.x * v1.y) - (v1.x * v0.y);
    if (abs(denom) <= 9.9999997473787516355514526367188e-05)
    {
        return float3(-1.0);
    }
    float v = ((v2.x * v1.y) - (v1.x * v2.y)) / denom;
    float w = ((v0.x * v2.y) - (v2.x * v0.y)) / denom;
    return float3((1.0 - v) - w, v, w);
}

static inline __attribute__((always_inline))
bool barycentric_inside(thread const float3& bary)
{
    bool _518 = bary.x >= (-0.001000000047497451305389404296875);
    bool _524;
    if (_518)
    {
        _524 = bary.y >= (-0.001000000047497451305389404296875);
    }
    else
    {
        _524 = _518;
    }
    bool _530;
    if (_524)
    {
        _530 = bary.z >= (-0.001000000047497451305389404296875);
    }
    else
    {
        _530 = _524;
    }
    return _530;
}

static inline __attribute__((always_inline))
float2 perspective_uv(thread const float3& bary, thread const float2& uv0, thread const float2& uv1, thread const float2& uv2, thread const float& q0, thread const float& q1, thread const float& q2)
{
    float denom = fast::max(((bary.x * q0) + (bary.y * q1)) + (bary.z * q2), 9.9999997473787516355514526367188e-05);
    return ((((uv0 * bary.x) * q0) + ((uv1 * bary.y) * q1)) + ((uv2 * bary.z) * q2)) / float2(denom);
}

static inline __attribute__((always_inline))
SurfacePoint surface_point_for_pixel(thread const float2& pixel, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 4096u) == 0u)
    {
        bool param = true;
        float2 param_1 = (pixel - pc.rect.xy) / fast::max(pc.rect.zw, float2(1.0));
        return make_surface_point(param, param_1);
    }
    float2 param_2 = float2(0.0);
    float3 p00 = project_surface_corner(param_2, pc);
    float2 param_3 = float2(1.0, 0.0);
    float3 p10 = project_surface_corner(param_3, pc);
    float2 param_4 = float2(1.0);
    float3 p11 = project_surface_corner(param_4, pc);
    float2 param_5 = float2(0.0, 1.0);
    float3 p01 = project_surface_corner(param_5, pc);
    float2 param_6 = pixel;
    float2 param_7 = p00.xy;
    float2 param_8 = p10.xy;
    float2 param_9 = p11.xy;
    float3 tri0 = barycentric_coords(param_6, param_7, param_8, param_9);
    float3 param_10 = tri0;
    if (barycentric_inside(param_10))
    {
        float3 param_11 = tri0;
        float2 param_12 = float2(0.0);
        float2 param_13 = float2(1.0, 0.0);
        float2 param_14 = float2(1.0);
        float param_15 = p00.z;
        float param_16 = p10.z;
        float param_17 = p11.z;
        float2 uv = perspective_uv(param_11, param_12, param_13, param_14, param_15, param_16, param_17);
        bool param_18 = true;
        float2 param_19 = uv;
        return make_surface_point(param_18, param_19);
    }
    float2 param_20 = pixel;
    float2 param_21 = p00.xy;
    float2 param_22 = p11.xy;
    float2 param_23 = p01.xy;
    float3 tri1 = barycentric_coords(param_20, param_21, param_22, param_23);
    float3 param_24 = tri1;
    if (barycentric_inside(param_24))
    {
        float3 param_25 = tri1;
        float2 param_26 = float2(0.0);
        float2 param_27 = float2(1.0);
        float2 param_28 = float2(0.0, 1.0);
        float param_29 = p00.z;
        float param_30 = p11.z;
        float param_31 = p01.z;
        float2 uv_1 = perspective_uv(param_25, param_26, param_27, param_28, param_29, param_30, param_31);
        bool param_32 = true;
        float2 param_33 = uv_1;
        return make_surface_point(param_32, param_33);
    }
    bool param_34 = false;
    float2 param_35 = float2(0.0);
    return make_surface_point(param_34, param_35);
}

static inline __attribute__((always_inline))
float positive_angle(thread const float& angle)
{
    return mod(mod(angle, 6.283185482025146484375) + 6.283185482025146484375, 6.283185482025146484375);
}

static inline __attribute__((always_inline))
float circular_progress_relative_angle(thread const float2& p, thread const float4& arcParams)
{
    float angle = precise::atan2(p.y, p.x);
    float _1205;
    if (arcParams.w >= 0.0)
    {
        float param = angle - arcParams.z;
        _1205 = positive_angle(param);
    }
    else
    {
        float param_1 = arcParams.z - angle;
        _1205 = positive_angle(param_1);
    }
    return _1205;
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
    float sweep = 6.283185482025146484375 * progress;
    float2 param = p;
    float4 param_1 = arcParams;
    float relativeAngle = circular_progress_relative_angle(param, param_1);
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
float4 unpack_corner_radii(thread const uint& _packed)
{
    return float4(float(_packed & 255u), float((_packed >> 8u) & 255u), float((_packed >> 16u) & 255u), float((_packed >> 24u) & 255u));
}

static inline __attribute__((always_inline))
float sd_squircle_box(thread const float2& p, thread const float2& halfSize, thread const float& radius, thread const float& amount, thread const float& power)
{
    float safeRadius = fast::clamp(radius, 0.0, fast::min(halfSize.x, halfSize.y));
    float2 q = abs(p) - fast::max(halfSize - float2(safeRadius), float2(0.0));
    float2 outside = fast::max(q, float2(0.0));
    float roundedNorm = length(outside);
    if (amount <= 0.0)
        return roundedNorm + fast::min(fast::max(q.x, q.y), 0.0) - safeRadius;
    float cornerPower = mix(2.0, fast::clamp(power, 2.0, 5.0), fast::clamp(amount, 0.0, 1.0));
    float squircleNorm = (outside.x == 0.0 || outside.y == 0.0)
        ? outside.x + outside.y
        : powr(powr(outside.x, cornerPower) + powr(outside.y, cornerPower), 1.0 / cornerPower);
    return (mix(roundedNorm, squircleNorm, fast::clamp(amount, 0.0, 1.0)) + fast::min(fast::max(q.x, q.y), 0.0)) - safeRadius;
}

static inline __attribute__((always_inline))
float corner_radius_for(thread const float2& p, thread const float4& radii)
{
    if (p.y < 0.0)
    {
        float _765;
        if (p.x < 0.0)
        {
            _765 = radii.x;
        }
        else
        {
            _765 = radii.y;
        }
        return _765;
    }
    float _778;
    if (p.x < 0.0)
    {
        _778 = radii.w;
    }
    else
    {
        _778 = radii.z;
    }
    return _778;
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
        return roundedNorm + fast::min(fast::max(q.x, q.y), 0.0) - safeRadius;
    float cornerPower = mix(2.0, fast::clamp(power, 2.0, 5.0), fast::clamp(amount, 0.0, 1.0));
    float squircleNorm = (outside.x == 0.0 || outside.y == 0.0)
        ? outside.x + outside.y
        : powr(powr(outside.x, cornerPower) + powr(outside.y, cornerPower), 1.0 / cornerPower);
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
float sd_liquid_bridge(thread const float2& p, thread const float2& halfSize, thread const float& amount)
{
    float radius = fast::max(halfSize.y, 0.5);
    float centerOffset = fast::max(halfSize.x - radius, 0.0);
    float left = length(p - float2(-centerOffset, 0.0)) - radius;
    float right = length(p - float2(centerOffset, 0.0)) - radius;
    float centerDistance = centerOffset * 2.0;
    float surfaceGap = fast::max(centerDistance - (radius * 2.0), 0.0);
    float easedAmount = smoothstep(0.0, 1.0, fast::clamp(amount, 0.0, 1.0));
    float separationFactor = smoothstep(0.0, radius * 0.89999997615814208984375, centerDistance);
    float smoothing = mix((surfaceGap * 2.0) + 0.5, (surfaceGap * 2.0) + (radius * 0.949999988079071044921875), easedAmount) * separationFactor;
    if (smoothing <= 0.001000000047497451305389404296875)
    {
        return fast::min(left, right);
    }
    float blend = fast::clamp(0.5 + ((0.5 * (right - left)) / smoothing), 0.0, 1.0);
    return mix(right, left, blend) - ((smoothing * blend) * (1.0 - blend));
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
float primitive_sdf(thread const float2& pixel, thread const float4& rect, thread const uint& primitive, thread const float& cornerRadius, thread const float4& cornerRadii, thread const bool& useCornerRadii, thread const float4& shapeParams, constant Renderer2DConstants& pc)
{
    float2 halfSize = fast::max(rect.zw * 0.5, float2(0.5));
    float2 center = rect.xy + halfSize;
    float2 p = pixel - center;
    if (primitive == 3u)
    {
        float2 q = p / halfSize;
        return (length(q) - 1.0) * fast::min(halfSize.x, halfSize.y);
    }
    float _1580;
    if (primitive == 1u)
    {
        _1580 = 0.0;
    }
    else
    {
        _1580 = fast::clamp(cornerRadius, 0.0, fast::min(halfSize.x, halfSize.y));
    }
    float radius = _1580;
    float squircleAmount = fast::clamp(shapeParams.x, 0.0, 1.0);
    float _1600;
    if (shapeParams.y <= 0.001000000047497451305389404296875)
    {
        _1600 = 4.0;
    }
    else
    {
        _1600 = shapeParams.y;
    }
    float squirclePower = fast::clamp(_1600, 2.0, 5.0);
    if (primitive == 6u)
    {
        float2 param = p;
        float2 param_1 = halfSize;
        float param_2 = radius;
        float param_3 = squircleAmount;
        float param_4 = squirclePower;
        return sd_squircle_box(param, param_1, param_2, param_3, param_4);
    }
    if (primitive == 7u)
    {
        float2 param_5 = pixel;
        float4 param_6 = rect;
        float param_7 = radius;
        float param_8 = squircleAmount;
        float param_9 = squirclePower;
        float param_10 = shapeParams.z;
        float param_11 = shapeParams.w;
        return sd_notched_squircle(param_5, param_6, param_7, param_8, param_9, param_10, param_11);
    }
    if (primitive == 9u)
    {
        float2 param_12 = p;
        float2 param_13 = halfSize;
        float param_14 = pc.uvRect.x;
        return sd_liquid_bridge(param_12, param_13, param_14);
    }
    if ((primitive != 1u) && useCornerRadii)
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
float shape_sdf(thread const float2& pixel, constant Renderer2DConstants& pc)
{
    if (pc.data.x == 8u)
    {
        float2 param = pixel;
        float4 param_1 = pc.rect;
        float4 param_2 = pc.uvRect;
        return sd_circular_progress(param, param_1, param_2);
    }
    bool useCornerRadii = (pc.data.y & 65536u) != 0u;
    float4 _1709;
    if (useCornerRadii)
    {
        uint param_3 = pc.data.z;
        _1709 = unpack_corner_radii(param_3);
    }
    else
    {
        _1709 = float4(pc.effect0.x);
    }
    float4 cornerRadii = _1709;
    bool effect1Reserved = (pc.data.y & 36864u) != 0u;
    float4 _1730;
    if (effect1Reserved)
    {
        _1730 = float4(1.0, 4.0, 0.0, 0.0);
    }
    else
    {
        _1730 = pc.effect1;
    }
    float4 shapeParams = _1730;
    float2 param_4 = pixel;
    float4 param_5 = pc.rect;
    uint param_6 = pc.data.x;
    float param_7 = pc.effect0.x;
    float4 param_8 = cornerRadii;
    bool param_9 = useCornerRadii;
    float4 param_10 = shapeParams;
    return primitive_sdf(param_4, param_5, param_6, param_7, param_8, param_9, param_10, pc);
}

static inline __attribute__((always_inline))
float shape_mask_alpha(thread const float2& pixel, thread const float& softness, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 32768u) == 0u)
    {
        return 1.0;
    }
    uint primitive = (pc.data.z >> 8u) & 15u;
    float radius = float(pc.data.z & 255u);
    float squircleAmount = float((pc.data.z >> 12u) & 15u) / 15.0;
    float squirclePower = 2.0 + ((float((pc.data.z >> 16u) & 15u) / 15.0) * 3.0);
    float notchAmount = float((pc.data.z >> 20u) & 15u) / 15.0;
    float notchDepth = float((pc.data.z >> 24u) & 255u);
    float4 maskParams = float4(squircleAmount, squirclePower, notchAmount, notchDepth);
    float2 param = pixel;
    float4 param_1 = pc.effect1;
    uint param_2 = primitive;
    float param_3 = radius;
    float4 param_4 = float4(radius);
    bool param_5 = false;
    float4 param_6 = maskParams;
    float sdf = primitive_sdf(param, param_1, param_2, param_3, param_4, param_5, param_6, pc);
    float inwardSoftness = fast::max(softness, 0.75);
    if ((pc.data.y & 536870912u) != 0u)
    {
        return smoothstep(0.0, inwardSoftness, sdf);
    }
    return 1.0 - smoothstep(-inwardSoftness, 0.0, sdf);
}

static inline __attribute__((always_inline))
float top_outline_factor_for_notch(thread const float2& pixel, thread const float& outlineWidth, thread const float& softness, constant Renderer2DConstants& pc)
{
    bool effect1Reserved = (pc.data.y & 36864u) != 0u;
    bool _2033;
    if (!effect1Reserved)
    {
        _2033 = pc.data.x != 7u;
    }
    else
    {
        _2033 = effect1Reserved;
    }
    bool _2040;
    if (!_2033)
    {
        _2040 = pc.effect1.z <= 0.00999999977648258209228515625;
    }
    else
    {
        _2040 = _2033;
    }
    if (_2040 || (outlineWidth <= 0.001000000047497451305389404296875))
    {
        return 1.0;
    }
    float notchSuppression = smoothstep(0.039999999105930328369140625, 0.2199999988079071044921875, fast::clamp(pc.effect1.z, 0.0, 1.0));
    float topDistance = pixel.y - pc.rect.y;
    float topEdgeFactor = smoothstep(outlineWidth + (softness * 0.25), outlineWidth + (softness * 1.75), topDistance);
    return mix(1.0, topEdgeFactor, notchSuppression);
}

static inline __attribute__((always_inline))
float circular_progress_gradient_t(thread const float2& pixel, constant Renderer2DConstants& pc)
{
    float2 halfSize = fast::max(pc.rect.zw * 0.5, float2(0.5));
    float2 p = pixel - (pc.rect.xy + halfSize);
    float progress = fast::clamp(pc.uvRect.x, 0.0, 1.0);
    float sweep = fast::max(6.283185482025146484375 * progress, 9.9999997473787516355514526367188e-06);
    float2 param = p;
    float4 param_1 = pc.uvRect;
    float relativeAngle = circular_progress_relative_angle(param, param_1);
    if (progress >= 0.999989986419677734375)
    {
        float loopT = fast::clamp(relativeAngle / 6.283185482025146484375, 0.0, 1.0);
        return 0.5 - (0.5 * cos(6.283185482025146484375 * loopT));
    }
    if ((relativeAngle <= sweep) || (progress >= 0.999989986419677734375))
    {
        return fast::clamp(relativeAngle / sweep, 0.0, 1.0);
    }
    float outerRadius = fast::max(fast::min(halfSize.x, halfSize.y), 0.5);
    float halfStroke = fast::clamp(pc.uvRect.y * 0.5, 0.0, outerRadius);
    float centerRadius = fast::max(outerRadius - halfStroke, 0.0);
    float direction = (pc.uvRect.w >= 0.0) ? 1.0 : (-1.0);
    float2 startPoint = float2(cos(pc.uvRect.z), sin(pc.uvRect.z)) * centerRadius;
    float endAngle = pc.uvRect.z + (direction * sweep);
    float2 endPoint = float2(cos(endAngle), sin(endAngle)) * centerRadius;
    return (length(p - startPoint) <= length(p - endPoint)) ? 0.0 : 1.0;
}

static inline __attribute__((always_inline))
float4 fill_color(thread const float2& uv, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 1u) == 0u)
    {
        return pc.color0;
    }
    if (pc.data.x == 8u)
    {
        float2 pixel = pc.rect.xy + (fast::clamp(uv, float2(0.0), float2(1.0)) * pc.rect.zw);
        float2 param = pixel;
        return mix(pc.color0, pc.color1, float4(circular_progress_gradient_t(param, pc)));
    }
    float2 a = pc.uvRect.xy;
    float2 b = pc.uvRect.zw;
    if ((pc.data.y & 2048u) != 0u)
    {
        float radius = fast::max(length(b - a), 9.9999997473787516355514526367188e-05);
        float t = fast::clamp(length(uv - a) / radius, 0.0, 1.0);
        t = smoothstep(0.0, 1.0, t);
        return mix(pc.color0, pc.color1, float4(t));
    }
    float2 axis = b - a;
    float denom = fast::max(dot(axis, axis), 9.9999997473787516355514526367188e-05);
    float t_1 = fast::clamp(dot(uv - a, axis) / denom, 0.0, 1.0);
    return mix(pc.color0, pc.color1, float4(t_1));
}

static inline __attribute__((always_inline))
float4 compose_shape_layers(thread const float4& body, thread const float& bodyCoverage, thread const float4& outline, thread const float& outlineCoverage)
{
    float bodyAlpha = body.w * bodyCoverage;
    float outlineAlpha = outline.w * outlineCoverage;
    float alpha = bodyAlpha + outlineAlpha;
    if (alpha <= 9.9999997473787516355514526367188e-05)
    {
        return float4(0.0);
    }
    float3 premultiplied = (body.xyz * bodyAlpha) + (outline.xyz * outlineAlpha);
    return float4(premultiplied / float3(alpha), alpha);
}

static inline __attribute__((always_inline))
float4 shade_shape_sample(thread const float2& samplePoint, constant Renderer2DConstants& pc)
{
    float2 param = samplePoint;
    SurfacePoint surface = surface_point_for_pixel(param, pc);
    if (!bool(surface.inside))
    {
        return float4(0.0);
    }
    float2 localPixel = pc.rect.xy + (surface.uv * pc.rect.zw);
    float2 param_1 = localPixel;
    float sdf = shape_sdf(param_1, pc);
    float softness = fast::max(pc.effect0.z, 0.5);
    float2 param_2 = samplePoint;
    float param_3 = softness;
    float maskAlpha = shape_mask_alpha(param_2, param_3, pc);
    if (maskAlpha <= 0.001000000047497451305389404296875)
    {
        return float4(0.0);
    }
    bool smoothEdges = (pc.data.y & 4u) != 0u;
    float _2126;
    if (smoothEdges)
    {
        _2126 = smoothstep(softness, -softness, sdf);
    }
    else
    {
        _2126 = float(sdf <= 0.0);
    }
    float fillAlpha = _2126;
    float outlineWidth = fast::max(pc.effect0.y, 0.0);
    float _2146;
    if (outlineWidth > 0.0)
    {
        float _2150;
        if (smoothEdges)
        {
            _2150 = smoothstep(softness, -softness, sdf + outlineWidth);
        }
        else
        {
            _2150 = float((sdf + outlineWidth) <= 0.0);
        }
        _2146 = _2150;
    }
    else
    {
        _2146 = fillAlpha;
    }
    float innerAlpha = _2146;
    float outlineAlpha = fast::max(fillAlpha - innerAlpha, 0.0);
    float bodyCoverage = (outlineWidth > 0.0) ? innerAlpha : fillAlpha;
    float2 param_4 = localPixel;
    float param_5 = outlineWidth;
    float param_6 = softness;
    float topOutlineFactor = top_outline_factor_for_notch(param_4, param_5, param_6, pc);
    bodyCoverage = mix(fillAlpha, bodyCoverage, topOutlineFactor);
    outlineAlpha *= topOutlineFactor;
    float2 uv = fast::clamp(surface.uv, float2(0.0), float2(1.0));
    float2 param_7 = uv;
    float4 body = fill_color(param_7, pc);
    float4 outline = pc.color2;
    float4 param_8 = body;
    float param_9 = bodyCoverage;
    float4 param_10 = outline;
    float param_11 = outlineAlpha;
    float4 src = compose_shape_layers(param_8, param_9, param_10, param_11);
    src.w *= (maskAlpha * pc.effect0.w);
    return (src.w > 0.001000000047497451305389404296875) ? src : float4(0.0);
}

static inline __attribute__((always_inline))
void accumulate_shape_sample(thread const float2& samplePoint, thread const float& weight, thread float3& premultiplied, thread float& alpha, thread float& weightSum, constant Renderer2DConstants& pc)
{
    float2 param = samplePoint;
    float4 sampleColor = shade_shape_sample(param, pc);
    premultiplied += ((sampleColor.xyz * sampleColor.w) * weight);
    alpha += (sampleColor.w * weight);
    weightSum += weight;
}

static inline __attribute__((always_inline))
float4 resolve_accumulated_shape(thread float3& premultiplied, thread float& alpha, thread const float& weightSum)
{
    alpha /= fast::max(weightSum, 9.9999997473787516355514526367188e-05);
    premultiplied /= float3(fast::max(weightSum, 9.9999997473787516355514526367188e-05));
    float3 _2267;
    if (alpha > 9.9999997473787516355514526367188e-05)
    {
        _2267 = premultiplied / float3(alpha);
    }
    else
    {
        _2267 = float3(0.0);
    }
    float3 rgb = _2267;
    return float4(rgb, alpha);
}

static inline __attribute__((always_inline))
float4 resolve_filtered_shape(thread const float2& pixelCenter, constant Renderer2DConstants& pc)
{
    float3 premultiplied = float3(0.0);
    float alpha = 0.0;
    float weightSum = 0.0;
    float2 param = pixelCenter + float2(-0.375, -0.125);
    float param_1 = 0.25;
    float3 param_2 = premultiplied;
    float param_3 = alpha;
    float param_4 = weightSum;
    accumulate_shape_sample(param, param_1, param_2, param_3, param_4, pc);
    premultiplied = param_2;
    alpha = param_3;
    weightSum = param_4;
    float2 param_5 = pixelCenter + float2(0.125, -0.375);
    float param_6 = 0.25;
    float3 param_7 = premultiplied;
    float param_8 = alpha;
    float param_9 = weightSum;
    accumulate_shape_sample(param_5, param_6, param_7, param_8, param_9, pc);
    premultiplied = param_7;
    alpha = param_8;
    weightSum = param_9;
    float2 param_10 = pixelCenter + float2(-0.125, 0.375);
    float param_11 = 0.25;
    float3 param_12 = premultiplied;
    float param_13 = alpha;
    float param_14 = weightSum;
    accumulate_shape_sample(param_10, param_11, param_12, param_13, param_14, pc);
    premultiplied = param_12;
    alpha = param_13;
    weightSum = param_14;
    float2 param_15 = pixelCenter + float2(0.375, 0.125);
    float param_16 = 0.25;
    float3 param_17 = premultiplied;
    float param_18 = alpha;
    float param_19 = weightSum;
    accumulate_shape_sample(param_15, param_16, param_17, param_18, param_19, pc);
    premultiplied = param_17;
    alpha = param_18;
    weightSum = param_19;
    float3 param_20 = premultiplied;
    float param_21 = alpha;
    float param_22 = weightSum;
    float4 _2358 = resolve_accumulated_shape(param_20, param_21, param_22);
    return _2358;
}

static inline __attribute__((always_inline))
float4 resolve_transformed_shape(thread const float2& pixelCenter, constant Renderer2DConstants& pc)
{
    float2 param = pixelCenter;
    return resolve_filtered_shape(param, pc);
}

static inline __attribute__((always_inline))
float4 resolve_blurred_shape(thread const float2& pixelCenter, constant Renderer2DConstants& pc)
{
    float radius = fast::clamp(pc.effect0.z, 0.0, 40.0);
    if (radius <= 0.5)
    {
        float2 param = pixelCenter;
        return shade_shape_sample(param, pc);
    }
    float3 premultiplied = float3(0.0);
    float alpha = 0.0;
    float weightSum = 0.0;
    float nearRadius = radius * 0.36000001430511474609375;
    float midRadius = radius * 0.62000000476837158203125;
    float farRadius = radius * 0.89999997615814208984375;
    float2 param_1 = pixelCenter;
    float param_2 = 0.180000007152557373046875;
    float3 param_3 = premultiplied;
    float param_4 = alpha;
    float param_5 = weightSum;
    accumulate_shape_sample(param_1, param_2, param_3, param_4, param_5, pc);
    premultiplied = param_3;
    alpha = param_4;
    weightSum = param_5;
    float2 param_6 = pixelCenter + float2(nearRadius, 0.0);
    float param_7 = 0.100000001490116119384765625;
    float3 param_8 = premultiplied;
    float param_9 = alpha;
    float param_10 = weightSum;
    accumulate_shape_sample(param_6, param_7, param_8, param_9, param_10, pc);
    premultiplied = param_8;
    alpha = param_9;
    weightSum = param_10;
    float2 param_11 = pixelCenter + float2(-nearRadius, 0.0);
    float param_12 = 0.100000001490116119384765625;
    float3 param_13 = premultiplied;
    float param_14 = alpha;
    float param_15 = weightSum;
    accumulate_shape_sample(param_11, param_12, param_13, param_14, param_15, pc);
    premultiplied = param_13;
    alpha = param_14;
    weightSum = param_15;
    float2 param_16 = pixelCenter + float2(0.0, nearRadius);
    float param_17 = 0.100000001490116119384765625;
    float3 param_18 = premultiplied;
    float param_19 = alpha;
    float param_20 = weightSum;
    accumulate_shape_sample(param_16, param_17, param_18, param_19, param_20, pc);
    premultiplied = param_18;
    alpha = param_19;
    weightSum = param_20;
    float2 param_21 = pixelCenter + float2(0.0, -nearRadius);
    float param_22 = 0.100000001490116119384765625;
    float3 param_23 = premultiplied;
    float param_24 = alpha;
    float param_25 = weightSum;
    accumulate_shape_sample(param_21, param_22, param_23, param_24, param_25, pc);
    premultiplied = param_23;
    alpha = param_24;
    weightSum = param_25;
    float2 param_26 = pixelCenter + float2(midRadius);
    float param_27 = 0.070000000298023223876953125;
    float3 param_28 = premultiplied;
    float param_29 = alpha;
    float param_30 = weightSum;
    accumulate_shape_sample(param_26, param_27, param_28, param_29, param_30, pc);
    premultiplied = param_28;
    alpha = param_29;
    weightSum = param_30;
    float2 param_31 = pixelCenter + float2(-midRadius, midRadius);
    float param_32 = 0.070000000298023223876953125;
    float3 param_33 = premultiplied;
    float param_34 = alpha;
    float param_35 = weightSum;
    accumulate_shape_sample(param_31, param_32, param_33, param_34, param_35, pc);
    premultiplied = param_33;
    alpha = param_34;
    weightSum = param_35;
    float2 param_36 = pixelCenter + float2(midRadius, -midRadius);
    float param_37 = 0.070000000298023223876953125;
    float3 param_38 = premultiplied;
    float param_39 = alpha;
    float param_40 = weightSum;
    accumulate_shape_sample(param_36, param_37, param_38, param_39, param_40, pc);
    premultiplied = param_38;
    alpha = param_39;
    weightSum = param_40;
    float2 param_41 = pixelCenter + float2(-midRadius, -midRadius);
    float param_42 = 0.070000000298023223876953125;
    float3 param_43 = premultiplied;
    float param_44 = alpha;
    float param_45 = weightSum;
    accumulate_shape_sample(param_41, param_42, param_43, param_44, param_45, pc);
    premultiplied = param_43;
    alpha = param_44;
    weightSum = param_45;
    float2 param_46 = pixelCenter + float2(farRadius, 0.0);
    float param_47 = 0.0350000001490116119384765625;
    float3 param_48 = premultiplied;
    float param_49 = alpha;
    float param_50 = weightSum;
    accumulate_shape_sample(param_46, param_47, param_48, param_49, param_50, pc);
    premultiplied = param_48;
    alpha = param_49;
    weightSum = param_50;
    float2 param_51 = pixelCenter + float2(-farRadius, 0.0);
    float param_52 = 0.0350000001490116119384765625;
    float3 param_53 = premultiplied;
    float param_54 = alpha;
    float param_55 = weightSum;
    accumulate_shape_sample(param_51, param_52, param_53, param_54, param_55, pc);
    premultiplied = param_53;
    alpha = param_54;
    weightSum = param_55;
    float2 param_56 = pixelCenter + float2(0.0, farRadius);
    float param_57 = 0.0350000001490116119384765625;
    float3 param_58 = premultiplied;
    float param_59 = alpha;
    float param_60 = weightSum;
    accumulate_shape_sample(param_56, param_57, param_58, param_59, param_60, pc);
    premultiplied = param_58;
    alpha = param_59;
    weightSum = param_60;
    float2 param_61 = pixelCenter + float2(0.0, -farRadius);
    float param_62 = 0.0350000001490116119384765625;
    float3 param_63 = premultiplied;
    float param_64 = alpha;
    float param_65 = weightSum;
    accumulate_shape_sample(param_61, param_62, param_63, param_64, param_65, pc);
    premultiplied = param_63;
    alpha = param_64;
    weightSum = param_65;
    float3 param_66 = premultiplied;
    float param_67 = alpha;
    float param_68 = weightSum;
    float4 _2617 = resolve_accumulated_shape(param_66, param_67, param_68);
    return _2617;
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

kernel void shape_2d_comp(constant Renderer2DConstants& pc [[buffer(0)]], texture2d<float, access::read_write> colorBuffer [[texture(0)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    int2 pixel = dispatch_origin(pc) + int2(gl_GlobalInvocationID.xy);
    int2 screenSize = int2(colorBuffer.get_width(), colorBuffer.get_height());
    bool _2640 = pixel.x < 0;
    bool _2647;
    if (!_2640)
    {
        _2647 = pixel.y < 0;
    }
    else
    {
        _2647 = _2640;
    }
    bool _2656;
    if (!_2647)
    {
        _2656 = pixel.x >= screenSize.x;
    }
    else
    {
        _2656 = _2647;
    }
    bool _2665;
    if (!_2656)
    {
        _2665 = pixel.y >= screenSize.y;
    }
    else
    {
        _2665 = _2656;
    }
    if (_2665)
    {
        return;
    }
    float2 p = float2(pixel) + float2(0.5);
    bool transformed = (pc.data.y & 4096u) != 0u;
    bool effect1Reserved = (pc.data.y & 36864u) != 0u;
    float4 _2685;
    if (effect1Reserved)
    {
        _2685 = float4(1.0, 4.0, 0.0, 0.0);
    }
    else
    {
        _2685 = pc.effect1;
    }
    float4 shapeParams = _2685;
    float4 param = shapeParams;
    float boundsPad = (pc.effect0.y + pc.effect0.z) + primitive_bounds_padding(param, pc);
    bool _2703 = !transformed;
    bool _2752;
    if (_2703)
    {
        bool _2712 = p.x < (pc.rect.x - boundsPad);
        bool _2723;
        if (!_2712)
        {
            _2723 = p.y < (pc.rect.y - boundsPad);
        }
        else
        {
            _2723 = _2712;
        }
        bool _2737;
        if (!_2723)
        {
            _2737 = p.x > ((pc.rect.x + pc.rect.z) + boundsPad);
        }
        else
        {
            _2737 = _2723;
        }
        bool _2751;
        if (!_2737)
        {
            _2751 = p.y > ((pc.rect.y + pc.rect.w) + boundsPad);
        }
        else
        {
            _2751 = _2737;
        }
        _2752 = _2751;
    }
    else
    {
        _2752 = _2703;
    }
    if (_2752)
    {
        return;
    }
    bool _2761 = (pc.data.y & 32u) != 0u;
    bool _2767;
    if (_2761)
    {
        _2767 = pc.effect0.z > 0.5;
    }
    else
    {
        _2767 = _2761;
    }
    bool foregroundBlur = _2767;
    float4 _2770;
    if (transformed)
    {
        float2 param_1 = p;
        _2770 = resolve_transformed_shape(param_1, pc);
    }
    else
    {
        float4 _2778;
        if (foregroundBlur)
        {
            float2 param_2 = p;
            _2778 = resolve_blurred_shape(param_2, pc);
        }
        else
        {
            float2 param_3 = p;
            _2778 = resolve_filtered_shape(param_3, pc);
        }
        _2770 = _2778;
    }
    float4 src = _2770;
    if (src.w <= 0.001000000047497451305389404296875)
    {
        return;
    }
    spvImageFence(colorBuffer);
    float4 dst = colorBuffer.read(uint2(pixel));
    float4 param_4 = dst;
    float4 param_5 = src;
    colorBuffer.write(blend_over(param_4, param_5), uint2(pixel));
}

