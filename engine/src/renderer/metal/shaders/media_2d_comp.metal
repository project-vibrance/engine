#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wmissing-braces"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

template<typename T, size_t Num>
struct spvUnsafeArray
{
    T elements[Num ? Num : 1];
    
    thread T& operator [] (size_t pos) thread
    {
        return elements[pos];
    }
    constexpr const thread T& operator [] (size_t pos) const thread
    {
        return elements[pos];
    }
    
    device T& operator [] (size_t pos) device
    {
        return elements[pos];
    }
    constexpr const device T& operator [] (size_t pos) const device
    {
        return elements[pos];
    }
    
    constexpr const constant T& operator [] (size_t pos) const constant
    {
        return elements[pos];
    }
    
    threadgroup T& operator [] (size_t pos) threadgroup
    {
        return elements[pos];
    }
    constexpr const threadgroup T& operator [] (size_t pos) const threadgroup
    {
        return elements[pos];
    }
};

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

constant spvUnsafeArray<float2, 4> _2960 = spvUnsafeArray<float2, 4>({ float2(-0.375, -0.125), float2(0.125, -0.375), float2(-0.125, 0.375), float2(0.375, 0.125) });

static inline __attribute__((always_inline))
int2 dispatch_origin(constant Renderer2DConstants& pc)
{
    return int2(int(pc.data.w & 65535u), int((pc.data.w >> 16u) & 65535u));
}

static inline __attribute__((always_inline))
bool media_premultiplied_alpha(constant Renderer2DConstants& pc)
{
    return (pc.data.y & 4194304u) != 0u;
}

static inline __attribute__((always_inline))
float media_texture_lod(constant Renderer2DConstants& pc, texture2d<float> mediaTexture, sampler mediaTextureSmplr)
{
    float2 sourceSize = float2(int2(mediaTexture.get_width(), mediaTexture.get_height()));
    float2 visibleSourceSize = abs(pc.uvRect.zw - pc.uvRect.xy) * sourceSize;
    float2 texelsPerScreenPixel = visibleSourceSize / fast::max(pc.rect.zw, float2(1.0));
    float footprint = fast::max(texelsPerScreenPixel.x, texelsPerScreenPixel.y);
    float maximumLod = float(max((int(mediaTexture.get_num_mip_levels()) - 1), 0));
    return fast::clamp(log2(fast::max(footprint, 1.0)), 0.0, maximumLod);
}

static inline __attribute__((always_inline))
float4 sample_media_texture_linear(thread const float2& uv, constant Renderer2DConstants& pc, texture2d<float> mediaTexture, sampler mediaTextureSmplr)
{
    return mediaTexture.sample(mediaTextureSmplr, uv, level(media_texture_lod(pc, mediaTexture, mediaTextureSmplr)));
}

static inline __attribute__((always_inline))
float4 sample_media_blur_horizontal(thread const float2& samplePoint, constant Renderer2DConstants& pc, texture2d<float> mediaTexture, sampler mediaTextureSmplr)
{
    int radius = clamp(int(round(pc.effect0.z)), 0, 32);
    int _2253 = max(1, (radius / 6));
    float sigma = fast::max(float(radius) * 0.5, 1.0);
    float2 localUv = fast::clamp((samplePoint - pc.rect.xy) / fast::max(pc.rect.zw, float2(1.0)), float2(0.0), float2(1.0));
    float2 uv = mix(pc.uvRect.xy, pc.uvRect.zw, localUv);
    float2 uvPerScreenPixel = abs(pc.uvRect.zw - pc.uvRect.xy) / fast::max(pc.rect.zw, float2(1.0));
    float2 uvMin = fast::min(pc.uvRect.xy, pc.uvRect.zw);
    float2 uvMax = fast::max(pc.uvRect.xy, pc.uvRect.zw);
    bool premultiplied = media_premultiplied_alpha(pc);
    float3 weightedRgb = float3(0.0);
    float weightedAlpha = 0.0;
    float weightSum = 0.0;
    int _2317 = -radius;
    float3 _2354;
    for (int stepSize = _2253, x = _2317; x <= radius; x += stepSize)
    {
        float weight = exp((-float(x * x)) / ((2.0 * sigma) * sigma));
        float2 sampleUv = fast::clamp(uv + (float2(float(x), 0.0) * uvPerScreenPixel), uvMin, uvMax);
        float2 param = sampleUv;
        float4 sampleColor = sample_media_texture_linear(param, pc, mediaTexture, mediaTextureSmplr);
        if (premultiplied)
        {
            _2354 = sampleColor.xyz;
        }
        else
        {
            _2354 = sampleColor.xyz * sampleColor.w;
        }
        weightedRgb += (_2354 * weight);
        weightedAlpha += (sampleColor.w * weight);
        weightSum += weight;
    }
    return float4(weightedRgb / float3(fast::max(weightSum, 9.9999997473787516355514526367188e-05)), weightedAlpha / fast::max(weightSum, 9.9999997473787516355514526367188e-05));
}

static inline __attribute__((always_inline))
float media_black_background_factor(constant Renderer2DConstants& pc)
{
    return float((pc.data.y & 16384u) != 0u);
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
    bool _542 = bary.x >= (-0.001000000047497451305389404296875);
    bool _548;
    if (_542)
    {
        _548 = bary.y >= (-0.001000000047497451305389404296875);
    }
    else
    {
        _548 = _542;
    }
    bool _554;
    if (_548)
    {
        _554 = bary.z >= (-0.001000000047497451305389404296875);
    }
    else
    {
        _554 = _548;
    }
    return _554;
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
float sd_rounded_box(thread const float2& p, thread const float2& halfSize, thread const float& radius)
{
    float2 q = abs(p) - fast::max(halfSize - float2(radius), float2(0.0));
    return (length(fast::max(q, float2(0.0))) + fast::min(fast::max(q.x, q.y), 0.0)) - radius;
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
float rounded_rect_mask(thread const float2& p, constant Renderer2DConstants& pc)
{
    float radius = fast::clamp(pc.effect0.x, 0.0, fast::min(pc.rect.z, pc.rect.w) * 0.5);
    float softness = fast::max(pc.effect0.y, 0.0);
    float2 halfSize = pc.rect.zw * 0.5;
    float2 local = p - (pc.rect.xy + halfSize);
    float2 q = abs(local) - (halfSize - float2(radius));
    float distanceToEdge = (length(fast::max(q, float2(0.0))) + fast::min(fast::max(q.x, q.y), 0.0)) - radius;
    if (softness <= 0.001000000047497451305389404296875)
    {
        return float(distanceToEdge <= 0.0);
    }
    return 1.0 - smoothstep(-softness, softness, distanceToEdge);
}

static inline __attribute__((always_inline))
float media_primitive_mask(thread const float2& p, constant Renderer2DConstants& pc)
{
    uint primitive = pc.data.x & 15u;
    if (primitive == 1u)
    {
        return ((step(pc.rect.x, p.x) * step(pc.rect.y, p.y)) * step(p.x, pc.rect.x + pc.rect.z)) * step(p.y, pc.rect.y + pc.rect.w);
    }
    if (primitive == 10u)
    {
        float barWidth = fast::max(pc.color2.z, 0.0);
        float gap = fast::max(pc.color2.w, 0.0);
        float stride = barWidth + gap;
        if ((barWidth <= 0.0) || (stride <= 0.0))
        {
            return 0.0;
        }
        float localX = p.x - pc.rect.x;
        int barIndex = int(floor(localX / stride));
        if ((barIndex < 0) || (barIndex >= 6))
        {
            return 0.0;
        }
        float barLeft = float(barIndex) * stride;
        bool _977 = localX < barLeft;
        bool _986;
        if (!_977)
        {
            _986 = localX > (barLeft + barWidth);
        }
        else
        {
            _986 = _977;
        }
        if (_986)
        {
            return 0.0;
        }
        float _993;
        if (barIndex < 4)
        {
            _993 = pc.color1[barIndex];
        }
        else
        {
            _993 = pc.color2[barIndex - 4];
        }
        float barHeight = _993;
        float2 barHalfSize = fast::max(float2(barWidth, barHeight) * 0.5, float2(0.5));
        float2 barCenter = float2((pc.rect.x + barLeft) + (barWidth * 0.5), pc.rect.y + (pc.rect.w * 0.5));
        float2 param = p - barCenter;
        float2 param_1 = barHalfSize;
        float param_2 = fast::min(barHalfSize.x, barHalfSize.y);
        float sdf = sd_rounded_box(param, param_1, param_2);
        return 1.0 - smoothstep(-0.75, 0.0, sdf);
    }
    float2 halfSize = fast::max(pc.rect.zw * 0.5, float2(0.5));
    float2 local = p - (pc.rect.xy + halfSize);
    float softness = fast::max(pc.effect0.y, 0.0);
    float sdf_1;
    if (primitive == 3u)
    {
        float2 q = local / halfSize;
        sdf_1 = (length(q) - 1.0) * fast::min(halfSize.x, halfSize.y);
    }
    else
    {
        if (primitive == 6u)
        {
            float radius = fast::clamp(pc.effect0.x, 0.0, fast::min(halfSize.x, halfSize.y));
            float2 param_3 = local;
            float2 param_4 = halfSize;
            float param_5 = radius;
            float param_6 = 1.0;
            float param_7 = 5.0;
            sdf_1 = sd_squircle_box(param_3, param_4, param_5, param_6, param_7);
        }
        else
        {
            float2 param_8 = p;
            return rounded_rect_mask(param_8, pc);
        }
    }
    if (softness <= 0.001000000047497451305389404296875)
    {
        return float(sdf_1 <= 0.0);
    }
    return 1.0 - smoothstep(0.0, softness, sdf_1);
}

static inline __attribute__((always_inline))
float4 decode_notched_mask_params(constant Renderer2DConstants& pc)
{
    uint _packed = uint(fast::max(pc.color2.z, 0.0) + 0.5);
    float squircleAmount = float(_packed & 15u) / 15.0;
    float squirclePower = 2.0 + ((float((_packed >> 4u) & 15u) / 15.0) * 3.0);
    float notchAmount = float((_packed >> 8u) & 15u) / 15.0;
    return float4(squircleAmount, squirclePower, notchAmount, pc.color2.w);
}

static inline __attribute__((always_inline))
float notch_flare_size(thread const float2& size, thread const float& amount, thread const float& depth)
{
    float progress = fast::clamp((fast::clamp(amount, 0.0, 1.0) - 0.85000002384185791015625) / 0.1500000059604644775390625, 0.0, 1.0);
    float _1230;
    if (depth > 0.5)
    {
        _1230 = depth;
    }
    else
    {
        _1230 = fast::clamp(size.y * 0.62000000476837158203125, 0.0, 92.0);
    }
    float depthTarget = _1230;
    float flareTarget = fast::clamp(depthTarget * 0.180000007152557373046875, 0.0, size.y * 0.5);
    float sizeRatio = fast::clamp((size.y * 0.5) / fast::max(flareTarget, 0.001000000047497451305389404296875), 0.0, 1.0);
    float flareT = sizeRatio * progress;
    return (flareTarget * flareT) * flareT;
}

static inline __attribute__((always_inline))
float corner_radius_for(thread const float2& p, thread const float4& radii)
{
    if (p.y < 0.0)
    {
        float _1133;
        if (p.x < 0.0)
        {
            _1133 = radii.x;
        }
        else
        {
            _1133 = radii.y;
        }
        return _1133;
    }
    float _1146;
    if (p.x < 0.0)
    {
        _1146 = radii.w;
    }
    else
    {
        _1146 = radii.z;
    }
    return _1146;
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
float sd_notched_squircle(thread const float2& pixel, thread const float4& rect, thread const float& cornerRadius, thread const float& squircleAmount, thread const float& squirclePower, thread const float& notchAmount, thread const float& notchDepth)
{
    float2 size = fast::max(rect.zw, float2(1.0));
    float2 halfSize = fast::max(size * 0.5, float2(0.5));
    float2 center = rect.xy + halfSize;
    float radius = fast::clamp(cornerRadius, 0.0, fast::min(halfSize.x, halfSize.y));
    float amount = fast::clamp(notchAmount, 0.0, 1.0);
    if (amount <= 0.00999999977648258209228515625)
    {
        float2 param = pixel - center;
        float2 param_1 = halfSize;
        float param_2 = radius;
        float param_3 = squircleAmount;
        float param_4 = squirclePower;
        return sd_squircle_box(param, param_1, param_2, param_3, param_4);
    }
    float2 param_5 = pixel - center;
    float2 param_6 = halfSize;
    float param_7 = radius;
    float param_8 = amount;
    float param_9 = squircleAmount;
    float param_10 = squirclePower;
    float param_11 = notchDepth;
    return sd_dynamic_notch(param_5, param_6, param_7, param_8, param_9, param_10, param_11);
}

static inline __attribute__((always_inline))
float inherited_shape_mask_alpha(thread const float2& pixel, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 32768u) == 0u)
    {
        return 1.0;
    }
    uint primitive = uint(fast::max(pc.color2.x, 0.0) + 0.5);
    float4 rect = pc.effect1;
    float2 halfSize = fast::max(rect.zw * 0.5, float2(0.5));
    float2 p = pixel - (rect.xy + halfSize);
    float radius = fast::clamp(pc.color2.y, 0.0, fast::min(halfSize.x, halfSize.y));
    float sdf = 0.0;
    if (primitive == 3u)
    {
        float2 q = p / halfSize;
        sdf = (length(q) - 1.0) * fast::min(halfSize.x, halfSize.y);
    }
    else
    {
        if (primitive == 6u)
        {
            float2 param = p;
            float2 param_1 = halfSize;
            float param_2 = radius;
            float param_3 = pc.color2.z;
            float param_4 = pc.color2.w;
            sdf = sd_squircle_box(param, param_1, param_2, param_3, param_4);
        }
        else
        {
            if (primitive == 7u)
            {
                float4 maskParams = decode_notched_mask_params(pc);
                float2 param_5 = pixel;
                float4 param_6 = rect;
                float param_7 = radius;
                float param_8 = maskParams.x;
                float param_9 = maskParams.y;
                float param_10 = maskParams.z;
                float param_11 = maskParams.w;
                sdf = sd_notched_squircle(param_5, param_6, param_7, param_8, param_9, param_10, param_11);
            }
            else
            {
                if (primitive == 1u)
                {
                    float2 param_12 = p;
                    float2 param_13 = halfSize;
                    float param_14 = 0.0;
                    sdf = sd_rounded_box(param_12, param_13, param_14);
                }
                else
                {
                    float2 param_15 = p;
                    float2 param_16 = halfSize;
                    float param_17 = radius;
                    sdf = sd_rounded_box(param_15, param_16, param_17);
                }
            }
        }
    }
    return 1.0 - smoothstep(-0.75, 0.0, sdf);
}

static inline __attribute__((always_inline))
float media_geometry_coverage(thread const SurfacePoint& surface, thread const float2& samplePoint, constant Renderer2DConstants& pc)
{
    if (!bool(surface.inside))
    {
        return 0.0;
    }
    float2 param = pc.rect.xy + (surface.uv * pc.rect.zw);
    float2 param_1 = samplePoint;
    return media_primitive_mask(param, pc) * inherited_shape_mask_alpha(param_1, pc);
}

static inline __attribute__((always_inline))
float4 sample_media_blur_vertical(thread const float2& samplePoint, constant Renderer2DConstants& pc, texture2d<float, access::read_write> mediaBlurBuffer)
{
    int radius = clamp(int(round(pc.effect0.z)), 0, 32);
    int _2406 = max(1, (radius / 6));
    float sigma = fast::max(float(radius) * 0.5, 1.0);
    int2 sourceSize = int2(mediaBlurBuffer.get_width(), mediaBlurBuffer.get_height());
    int2 centerPixel = int2(floor(samplePoint));
    bool premultiplied = media_premultiplied_alpha(pc);
    float3 weightedRgb = float3(0.0);
    float weightedAlpha = 0.0;
    float weightSum = 0.0;
    int _2430 = -radius;
    for (int stepSize = _2406, y = _2430; y <= radius; y += stepSize)
    {
        float weight = exp((-float(y * y)) / ((2.0 * sigma) * sigma));
        int2 sourcePixel = clamp(centerPixel + int2(0, y), int2(0), sourceSize - int2(1));
        spvImageFence(mediaBlurBuffer);
        float4 sampleColor = mediaBlurBuffer.read(uint2(sourcePixel));
        weightedRgb += (sampleColor.xyz * weight);
        weightedAlpha += (sampleColor.w * weight);
        weightSum += weight;
    }
    float alpha = weightedAlpha / fast::max(weightSum, 9.9999997473787516355514526367188e-05);
    float3 _2490;
    if (premultiplied)
    {
        _2490 = weightedRgb / float3(fast::max(weightSum, 9.9999997473787516355514526367188e-05));
    }
    else
    {
        float3 _2501;
        if (weightedAlpha > 9.9999997473787516355514526367188e-05)
        {
            _2501 = weightedRgb / float3(weightedAlpha);
        }
        else
        {
            _2501 = float3(0.0);
        }
        _2490 = _2501;
    }
    float3 rgb = _2490;
    return float4(rgb, alpha);
}

static inline __attribute__((always_inline))
float4 catmull_rom_weights(thread const float& fraction)
{
    float f2 = fraction * fraction;
    float f3 = f2 * fraction;
    return float4((((-0.5) * fraction) + f2) - (0.5 * f3), (1.0 - (2.5 * f2)) + (1.5 * f3), ((0.5 * fraction) + (2.0 * f2)) - (1.5 * f3), ((-0.5) * f2) + (0.5 * f3));
}

static inline __attribute__((always_inline))
float4 sample_media_texture_cubic_level(thread const float2& uv, thread const int& lodLevel, texture2d<float> mediaTexture, sampler mediaTextureSmplr)
{
    float2 sourceSize = float2(int2(mediaTexture.get_width(lodLevel), mediaTexture.get_height(lodLevel)));
    float2 inverseSize = float2(1.0) / fast::max(sourceSize, float2(1.0));
    float2 samplePosition = fast::clamp(uv, float2(0.0), float2(1.0)) * sourceSize;
    float2 texel1 = floor(samplePosition - float2(0.5)) + float2(0.5);
    float2 fraction = samplePosition - texel1;
    float param = fraction.x;
    float4 weightsX = catmull_rom_weights(param);
    float param_1 = fraction.y;
    float4 weightsY = catmull_rom_weights(param_1);
    float2 centreWeights = float2(fast::max(weightsX.y + weightsX.z, 9.9999997473787516355514526367188e-05), fast::max(weightsY.y + weightsY.z, 9.9999997473787516355514526367188e-05));
    float2 centreOffset = float2(weightsX.z / centreWeights.x, weightsY.z / centreWeights.y);
    float3 sampleX = float3(texel1.x - 1.0, texel1.x + centreOffset.x, texel1.x + 2.0);
    float3 sampleY = float3(texel1.y - 1.0, texel1.y + centreOffset.y, texel1.y + 2.0);
    float3 combinedX = float3(weightsX.x, centreWeights.x, weightsX.w);
    float3 combinedY = float3(weightsY.x, centreWeights.y, weightsY.w);
    float2 minimumUv = inverseSize * 0.5;
    float2 maximumUv = float2(1.0) - minimumUv;
    float4 result = float4(0.0);
    for (int y = 0; y < 3; y++)
    {
        for (int x = 0; x < 3; x++)
        {
            float2 sampleUv = fast::clamp(float2(sampleX[x], sampleY[y]) * inverseSize, minimumUv, maximumUv);
            result += ((mediaTexture.sample(mediaTextureSmplr, sampleUv, level(float(lodLevel))) * combinedX[x]) * combinedY[y]);
        }
    }
    return fast::clamp(result, float4(0.0), float4(1.0));
}

static inline __attribute__((always_inline))
float4 sample_media_texture(thread const float2& uv, constant Renderer2DConstants& pc, texture2d<float> mediaTexture, sampler mediaTextureSmplr)
{
    float lod = media_texture_lod(pc, mediaTexture, mediaTextureSmplr);
    int lowerLod = int(floor(lod));
    int upperLod = min((lowerLod + 1), max((int(mediaTexture.get_num_mip_levels()) - 1), 0));
    float2 param = uv;
    int param_1 = lowerLod;
    float4 lower = sample_media_texture_cubic_level(param, param_1, mediaTexture, mediaTextureSmplr);
    if (upperLod == lowerLod)
    {
        return lower;
    }
    float2 param_2 = uv;
    int param_3 = upperLod;
    float4 upper = sample_media_texture_cubic_level(param_2, param_3, mediaTexture, mediaTextureSmplr);
    return mix(lower, upper, float4(fract(lod)));
}

static inline __attribute__((always_inline))
float4 sample_media(thread const float2& uv, constant Renderer2DConstants& pc, texture2d<float> mediaTexture, sampler mediaTextureSmplr)
{
    bool _2011 = (pc.data.y & 32u) == 0u;
    bool _2018;
    if (!_2011)
    {
        _2018 = pc.effect0.z <= 0.001000000047497451305389404296875;
    }
    else
    {
        _2018 = _2011;
    }
    if (_2018)
    {
        float2 param = uv;
        return sample_media_texture(param, pc, mediaTexture, mediaTextureSmplr);
    }
    int radius = clamp(int(round(pc.effect0.z)), 0, 32);
    if (radius <= 0)
    {
        float2 param_1 = uv;
        return sample_media_texture(param_1, pc, mediaTexture, mediaTextureSmplr);
    }
    int stepSize = max(1, (radius / 6));
    float sigma = fast::max(float(radius) * 0.5, 1.0);
    float2 uvPerScreenPixel = abs(pc.uvRect.zw - pc.uvRect.xy) / fast::max(pc.rect.zw, float2(1.0));
    float2 uvMin = fast::min(pc.uvRect.xy, pc.uvRect.zw);
    float2 uvMax = fast::max(pc.uvRect.xy, pc.uvRect.zw);
    bool premultiplied = media_premultiplied_alpha(pc);
    float3 weightedRgb = float3(0.0);
    float weightedAlpha = 0.0;
    float weightSum = 0.0;
    int tapCount = 0;
    int _2088 = -radius;
    spvUnsafeArray<float, 65> axisWeights;
    for (int offset = _2088; offset <= radius; offset += stepSize)
    {
        axisWeights[tapCount] = exp((-float(offset * offset)) / ((2.0 * sigma) * sigma));
        tapCount++;
    }
    float3 _2179;
    for (int yIndex = 0; yIndex < tapCount; yIndex++)
    {
        int y = (-radius) + (yIndex * stepSize);
        for (int xIndex = 0; xIndex < tapCount; xIndex++)
        {
            int x = (-radius) + (xIndex * stepSize);
            float2 offset_1 = float2(float(x), float(y));
            float weight = axisWeights[xIndex] * axisWeights[yIndex];
            float2 sampleUv = fast::clamp(uv + (offset_1 * uvPerScreenPixel), uvMin, uvMax);
            float2 param_2 = sampleUv;
            float4 sampleColor = sample_media_texture_linear(param_2, pc, mediaTexture, mediaTextureSmplr);
            if (premultiplied)
            {
                _2179 = sampleColor.xyz;
            }
            else
            {
                _2179 = sampleColor.xyz * sampleColor.w;
            }
            weightedRgb += (_2179 * weight);
            weightedAlpha += (sampleColor.w * weight);
            weightSum += weight;
        }
    }
    float alpha = weightedAlpha / fast::max(weightSum, 9.9999997473787516355514526367188e-05);
    float3 _2215;
    if (premultiplied)
    {
        _2215 = weightedRgb / float3(fast::max(weightSum, 9.9999997473787516355514526367188e-05));
    }
    else
    {
        float3 _2226;
        if (weightedAlpha > 9.9999997473787516355514526367188e-05)
        {
            _2226 = weightedRgb / float3(weightedAlpha);
        }
        else
        {
            _2226 = float3(0.0);
        }
        _2215 = _2226;
    }
    float3 rgb = _2215;
    return float4(rgb, alpha);
}

static inline __attribute__((always_inline))
float4 unpremultiply_media(thread float4& src, constant Renderer2DConstants& pc)
{
    if (!media_premultiplied_alpha(pc))
    {
        return src;
    }
    float3 _2528;
    if (src.w > 9.9999997473787516355514526367188e-05)
    {
        _2528 = src.xyz / float3(src.w);
    }
    else
    {
        _2528 = float3(0.0);
    }
    src.x = _2528.x;
    src.y = _2528.y;
    src.z = _2528.z;
    return src;
}

static inline __attribute__((always_inline))
float gradient_t(thread const float2& localUv, constant Renderer2DConstants& pc)
{
    float2 a = pc.color2.xy;
    float2 b = pc.color2.zw;
    if ((pc.data.y & 2048u) != 0u)
    {
        float radius = fast::max(length(b - a), 9.9999997473787516355514526367188e-05);
        return fast::clamp(length(localUv - a) / radius, 0.0, 1.0);
    }
    float2 axis = b - a;
    float denom = fast::max(dot(axis, axis), 9.9999997473787516355514526367188e-05);
    return fast::clamp(dot(localUv - a, axis) / denom, 0.0, 1.0);
}

static inline __attribute__((always_inline))
float3 media_tint_rgb(thread const float2& localUv, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 1u) == 0u)
    {
        return pc.color0.xyz;
    }
    float2 param = localUv;
    return mix(pc.color0.xyz, pc.color1.xyz, float3(gradient_t(param, pc)));
}

static inline __attribute__((always_inline))
float unpack_unorm8(thread const uint& _packed, thread const uint& shift)
{
    return float((_packed >> shift) & 255u) / 255.0;
}

static inline __attribute__((always_inline))
float unpack_range8(thread const uint& _packed, thread const uint& shift, thread const float& minValue, thread const float& maxValue)
{
    uint param = _packed;
    uint param_1 = shift;
    return mix(minValue, maxValue, unpack_unorm8(param, param_1));
}

static inline __attribute__((always_inline))
float3 apply_media_color_adjustment(thread const float3& rgb, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 8192u) == 0u)
    {
        return rgb;
    }
    uint _packed = pc.data.z;
    uint param = _packed;
    uint param_1 = 0u;
    float param_2 = -1.0;
    float param_3 = 1.0;
    float brightness = unpack_range8(param, param_1, param_2, param_3);
    uint param_4 = _packed;
    uint param_5 = 8u;
    float param_6 = 0.0;
    float param_7 = 4.0;
    float contrast = unpack_range8(param_4, param_5, param_6, param_7);
    uint param_8 = _packed;
    uint param_9 = 16u;
    float param_10 = -4.0;
    float param_11 = 4.0;
    float exposure = unpack_range8(param_8, param_9, param_10, param_11);
    uint param_12 = _packed;
    uint param_13 = 24u;
    float invertAmount = unpack_unorm8(param_12, param_13);
    float3 adjusted = rgb * exp2(exposure);
    adjusted = ((adjusted - float3(0.5)) * contrast) + float3(0.5);
    adjusted += float3(brightness);
    adjusted = mix(adjusted, float3(1.0) - adjusted, float3(invertAmount));
    return fast::clamp(adjusted, float3(0.0), float3(1.0));
}

static inline __attribute__((always_inline))
float3 apply_media_auto_black_lift(thread const float3& rgb, thread const float& alpha, thread const float& backgroundFactor)
{
    if (backgroundFactor <= 0.001000000047497451305389404296875)
    {
        return rgb;
    }
    float alphaWeight = smoothstep(0.0500000007450580596923828125, 0.3499999940395355224609375, alpha);
    float strength = backgroundFactor * alphaWeight;
    if (strength <= 0.001000000047497451305389404296875)
    {
        return rgb;
    }
    float luminance = dot(rgb, float3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875));
    float blackWeight = 1.0 - smoothstep(0.0379999987781047821044921875, 0.180000007152557373046875, luminance);
    float3 raisedBlack = fast::max(rgb, float3(0.0379999987781047821044921875));
    float3 balanced = mix(rgb, raisedBlack, float3(blackWeight * 0.920000016689300537109375));
    return fast::clamp(mix(rgb, balanced, float3(strength)), float3(0.0), float3(1.0));
}

static inline __attribute__((always_inline))
float media_alpha_mask(thread const float2& localUv, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 1u) == 0u)
    {
        return pc.color0.w;
    }
    float2 param = localUv;
    return mix(pc.color0.w, pc.color1.w, gradient_t(param, pc));
}

static inline __attribute__((always_inline))
float4 shade_media_surface(thread const SurfacePoint& surface, thread const float2& samplePoint, thread const float& backgroundFactor, constant Renderer2DConstants& pc, texture2d<float> mediaTexture, sampler mediaTextureSmplr, texture2d<float, access::read_write> mediaBlurBuffer)
{
    if (!bool(surface.inside))
    {
        return float4(0.0);
    }
    float2 localUv = fast::clamp(surface.uv, float2(0.0), float2(1.0));
    float2 uv = mix(pc.uvRect.xy, pc.uvRect.zw, localUv);
    float4 _2720;
    if ((pc.data.y & 268435456u) != 0u)
    {
        float2 param = pc.rect.xy + (localUv * pc.rect.zw);
        _2720 = sample_media_blur_vertical(param, pc, mediaBlurBuffer);
    }
    else
    {
        float2 param_1 = uv;
        _2720 = sample_media(param_1, pc, mediaTexture, mediaTextureSmplr);
    }
    float4 filtered = _2720;
    float4 param_2 = filtered;
    float4 _2742 = unpremultiply_media(param_2, pc);
    float4 src = _2742;
    float2 param_3 = localUv;
    float3 tintRgb = media_tint_rgb(param_3, pc);
    if ((pc.data.y & 524288u) != 0u)
    {
        src.x = tintRgb.x;
        src.y = tintRgb.y;
        src.z = tintRgb.z;
    }
    else
    {
        float4 _2763 = src;
        float3 _2765 = _2763.xyz * tintRgb;
        src.x = _2765.x;
        src.y = _2765.y;
        src.z = _2765.z;
    }
    float3 param_4 = src.xyz;
    float3 _2775 = apply_media_color_adjustment(param_4, pc);
    src.x = _2775.x;
    src.y = _2775.y;
    src.z = _2775.z;
    float3 param_5 = src.xyz;
    float param_6 = src.w;
    float param_7 = backgroundFactor;
    float3 _2790 = apply_media_auto_black_lift(param_5, param_6, param_7);
    src.x = _2790.x;
    src.y = _2790.y;
    src.z = _2790.z;
    float2 param_8 = localUv;
    src.w *= (media_alpha_mask(param_8, pc) * pc.effect0.w);
    return (src.w > 0.001000000047497451305389404296875) ? src : float4(0.0);
}

static inline __attribute__((always_inline))
float4 shade_media_sample(thread const float2& samplePoint, thread const float& backgroundFactor, constant Renderer2DConstants& pc, texture2d<float> mediaTexture, sampler mediaTextureSmplr, texture2d<float, access::read_write> mediaBlurBuffer)
{
    float2 param = samplePoint;
    SurfacePoint surface = surface_point_for_pixel(param, pc);
    SurfacePoint param_1 = surface;
    float2 param_2 = samplePoint;
    float coverage = media_geometry_coverage(param_1, param_2, pc);
    if (coverage <= 0.0)
    {
        return float4(0.0);
    }
    SurfacePoint param_3 = surface;
    float2 param_4 = samplePoint;
    float param_5 = backgroundFactor;
    float4 src = shade_media_surface(param_3, param_4, param_5, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    src.w *= coverage;
    return (src.w > 0.001000000047497451305389404296875) ? src : float4(0.0);
}

static inline __attribute__((always_inline))
float4 resolve_transformed_media(thread const float2& pixelCenter, thread const float& backgroundFactor, constant Renderer2DConstants& pc, texture2d<float> mediaTexture, sampler mediaTextureSmplr, texture2d<float, access::read_write> mediaBlurBuffer)
{
    float2 param = pixelCenter + float2(-0.375, -0.125);
    float param_1 = backgroundFactor;
    float4 s0 = shade_media_sample(param, param_1, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    float2 param_2 = pixelCenter + float2(0.125, -0.375);
    float param_3 = backgroundFactor;
    float4 s1 = shade_media_sample(param_2, param_3, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    float2 param_4 = pixelCenter + float2(-0.125, 0.375);
    float param_5 = backgroundFactor;
    float4 s2 = shade_media_sample(param_4, param_5, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    float2 param_6 = pixelCenter + float2(0.375, 0.125);
    float param_7 = backgroundFactor;
    float4 s3 = shade_media_sample(param_6, param_7, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    float3 premultiplied = (((s0.xyz * s0.w) + (s1.xyz * s1.w)) + (s2.xyz * s2.w)) + (s3.xyz * s3.w);
    float alpha = ((s0.w + s1.w) + s2.w) + s3.w;
    premultiplied *= 0.25;
    alpha *= 0.25;
    float3 _3163;
    if (alpha > 9.9999997473787516355514526367188e-05)
    {
        _3163 = premultiplied / float3(alpha);
    }
    else
    {
        _3163 = float3(0.0);
    }
    float3 rgb = _3163;
    return float4(rgb, alpha);
}

static inline __attribute__((always_inline))
void accumulate_media_sample(thread const float2& samplePoint, thread const float& weight, thread const float& backgroundFactor, thread float3& premultiplied, thread float& alpha, thread float& weightSum, constant Renderer2DConstants& pc, texture2d<float> mediaTexture, sampler mediaTextureSmplr, texture2d<float, access::read_write> mediaBlurBuffer)
{
    float2 param = samplePoint;
    float param_1 = backgroundFactor;
    float4 sampleColor = shade_media_sample(param, param_1, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    premultiplied += ((sampleColor.xyz * sampleColor.w) * weight);
    alpha += (sampleColor.w * weight);
    weightSum += weight;
}

static inline __attribute__((always_inline))
float4 resolve_accumulated_media(thread float3& premultiplied, thread float& alpha, thread const float& weightSum)
{
    alpha /= fast::max(weightSum, 9.9999997473787516355514526367188e-05);
    premultiplied /= float3(fast::max(weightSum, 9.9999997473787516355514526367188e-05));
    float3 _2909;
    if (alpha > 9.9999997473787516355514526367188e-05)
    {
        _2909 = premultiplied / float3(alpha);
    }
    else
    {
        _2909 = float3(0.0);
    }
    float3 rgb = _2909;
    return float4(rgb, alpha);
}

static inline __attribute__((always_inline))
float4 resolve_filtered_media(thread const float2& pixelCenter, thread const float& backgroundFactor, constant Renderer2DConstants& pc, texture2d<float> mediaTexture, sampler mediaTextureSmplr, texture2d<float, access::read_write> mediaBlurBuffer)
{
    bool _2930 = (pc.data.y & 67108864u) != 0u;
    bool _2937;
    if (_2930)
    {
        _2937 = (pc.data.y & 32u) != 0u;
    }
    else
    {
        _2937 = _2930;
    }
    if (_2937)
    {
        float coverage = 0.0;
        for (int index = 0; index < 4; index++)
        {
            float2 samplePoint = pixelCenter + _2960[index];
            float2 param = samplePoint;
            SurfacePoint surface = surface_point_for_pixel(param, pc);
            SurfacePoint param_1 = surface;
            float2 param_2 = samplePoint;
            coverage += media_geometry_coverage(param_1, param_2, pc);
        }
        if (coverage <= 0.0)
        {
            return float4(0.0);
        }
        float2 param_3 = pixelCenter;
        SurfacePoint centerSurface = surface_point_for_pixel(param_3, pc);
        SurfacePoint param_4 = centerSurface;
        float2 param_5 = pixelCenter;
        float param_6 = backgroundFactor;
        float4 src = shade_media_surface(param_4, param_5, param_6, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
        if (src.w <= 0.001000000047497451305389404296875)
        {
            return float4(0.0);
        }
        src.w *= (coverage * 0.25);
        return (src.w > 0.001000000047497451305389404296875) ? src : float4(0.0);
    }
    float3 premultiplied = float3(0.0);
    float alpha = 0.0;
    float weightSum = 0.0;
    float2 param_7 = pixelCenter + float2(-0.375, -0.125);
    float param_8 = 0.25;
    float param_9 = backgroundFactor;
    float3 param_10 = premultiplied;
    float param_11 = alpha;
    float param_12 = weightSum;
    accumulate_media_sample(param_7, param_8, param_9, param_10, param_11, param_12, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    premultiplied = param_10;
    alpha = param_11;
    weightSum = param_12;
    float2 param_13 = pixelCenter + float2(0.125, -0.375);
    float param_14 = 0.25;
    float param_15 = backgroundFactor;
    float3 param_16 = premultiplied;
    float param_17 = alpha;
    float param_18 = weightSum;
    accumulate_media_sample(param_13, param_14, param_15, param_16, param_17, param_18, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    premultiplied = param_16;
    alpha = param_17;
    weightSum = param_18;
    float2 param_19 = pixelCenter + float2(-0.125, 0.375);
    float param_20 = 0.25;
    float param_21 = backgroundFactor;
    float3 param_22 = premultiplied;
    float param_23 = alpha;
    float param_24 = weightSum;
    accumulate_media_sample(param_19, param_20, param_21, param_22, param_23, param_24, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    premultiplied = param_22;
    alpha = param_23;
    weightSum = param_24;
    float2 param_25 = pixelCenter + float2(0.375, 0.125);
    float param_26 = 0.25;
    float param_27 = backgroundFactor;
    float3 param_28 = premultiplied;
    float param_29 = alpha;
    float param_30 = weightSum;
    accumulate_media_sample(param_25, param_26, param_27, param_28, param_29, param_30, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    premultiplied = param_28;
    alpha = param_29;
    weightSum = param_30;
    float3 param_31 = premultiplied;
    float param_32 = alpha;
    float param_33 = weightSum;
    float4 _3089 = resolve_accumulated_media(param_31, param_32, param_33);
    return _3089;
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

kernel void media_2d_comp(constant Renderer2DConstants& pc [[buffer(0)]], texture2d<float> mediaTexture [[texture(0)]], texture2d<float, access::read_write> mediaBlurBuffer [[texture(1)]], texture2d<float, access::read_write> colorBuffer [[texture(2)]], sampler mediaTextureSmplr [[sampler(0)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    uint dispatchWidth = (pc.data.x >> 4u) & 16383u;
    uint dispatchHeight = (pc.data.x >> 18u) & 16383u;
    bool _3196 = (dispatchWidth > 0u) && (dispatchHeight > 0u);
    bool _3215;
    if (_3196)
    {
        bool _3206 = gl_GlobalInvocationID.x >= dispatchWidth;
        bool _3214;
        if (!_3206)
        {
            _3214 = gl_GlobalInvocationID.y >= dispatchHeight;
        }
        else
        {
            _3214 = _3206;
        }
        _3215 = _3214;
    }
    else
    {
        _3215 = _3196;
    }
    if (_3215)
    {
        return;
    }
    int2 pixel = dispatch_origin(pc) + int2(gl_GlobalInvocationID.xy);
    int2 screenSize = int2(colorBuffer.get_width(), colorBuffer.get_height());
    bool _3234 = pixel.x < 0;
    bool _3241;
    if (!_3234)
    {
        _3241 = pixel.y < 0;
    }
    else
    {
        _3241 = _3234;
    }
    bool _3250;
    if (!_3241)
    {
        _3250 = pixel.x >= screenSize.x;
    }
    else
    {
        _3250 = _3241;
    }
    bool _3259;
    if (!_3250)
    {
        _3259 = pixel.y >= screenSize.y;
    }
    else
    {
        _3259 = _3250;
    }
    if (_3259)
    {
        return;
    }
    float2 p = float2(pixel) + float2(0.5);
    if ((pc.data.y & 134217728u) != 0u)
    {
        float2 param = p;
        mediaBlurBuffer.write(sample_media_blur_horizontal(param, pc, mediaTexture, mediaTextureSmplr), uint2(pixel));
        return;
    }
    bool transformed = (pc.data.y & 4096u) != 0u;
    float samplePad = 1.0;
    bool _3287 = !transformed;
    bool _3336;
    if (_3287)
    {
        bool _3296 = p.x < (pc.rect.x - samplePad);
        bool _3307;
        if (!_3296)
        {
            _3307 = p.y < (pc.rect.y - samplePad);
        }
        else
        {
            _3307 = _3296;
        }
        bool _3321;
        if (!_3307)
        {
            _3321 = p.x > ((pc.rect.x + pc.rect.z) + samplePad);
        }
        else
        {
            _3321 = _3307;
        }
        bool _3335;
        if (!_3321)
        {
            _3335 = p.y > ((pc.rect.y + pc.rect.w) + samplePad);
        }
        else
        {
            _3335 = _3321;
        }
        _3336 = _3335;
    }
    else
    {
        _3336 = _3287;
    }
    if (_3336)
    {
        return;
    }
    float backgroundFactor = media_black_background_factor(pc);
    float4 _3344;
    if (transformed)
    {
        float2 param_1 = p;
        float param_2 = backgroundFactor;
        _3344 = resolve_transformed_media(param_1, param_2, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    }
    else
    {
        float2 param_3 = p;
        float param_4 = backgroundFactor;
        _3344 = resolve_filtered_media(param_3, param_4, pc, mediaTexture, mediaTextureSmplr, mediaBlurBuffer);
    }
    float4 src = _3344;
    if (src.w <= 0.001000000047497451305389404296875)
    {
        return;
    }
    spvImageFence(colorBuffer);
    float4 dst = colorBuffer.read(uint2(pixel));
    float4 param_5 = dst;
    float4 param_6 = src;
    colorBuffer.write(blend_over(param_5, param_6), uint2(pixel));
}

