// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

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

constant spvUnsafeArray<float, 41> _2350 = spvUnsafeArray<float, 41>({ -1.0, -0.949999988079071044921875, -0.89999997615814208984375, -0.85000002384185791015625, -0.800000011920928955078125, -0.75, -0.699999988079071044921875, -0.64999997615814208984375, -0.60000002384185791015625, -0.550000011920928955078125, -0.5, -0.449999988079071044921875, -0.4000000059604644775390625, -0.3499999940395355224609375, -0.300000011920928955078125, -0.25, -0.20000000298023223876953125, -0.1500000059604644775390625, -0.100000001490116119384765625, -0.0500000007450580596923828125, 0.0, 0.0500000007450580596923828125, 0.100000001490116119384765625, 0.1500000059604644775390625, 0.20000000298023223876953125, 0.25, 0.300000011920928955078125, 0.3499999940395355224609375, 0.4000000059604644775390625, 0.449999988079071044921875, 0.5, 0.550000011920928955078125, 0.60000002384185791015625, 0.64999997615814208984375, 0.699999988079071044921875, 0.75, 0.800000011920928955078125, 0.85000002384185791015625, 0.89999997615814208984375, 0.949999988079071044921875, 1.0 });
constant spvUnsafeArray<float, 21> _2359 = spvUnsafeArray<float, 21>({ -1.0, -0.89999997615814208984375, -0.800000011920928955078125, -0.699999988079071044921875, -0.60000002384185791015625, -0.5, -0.4000000059604644775390625, -0.300000011920928955078125, -0.20000000298023223876953125, -0.100000001490116119384765625, 0.0, 0.100000001490116119384765625, 0.20000000298023223876953125, 0.300000011920928955078125, 0.4000000059604644775390625, 0.5, 0.60000002384185791015625, 0.699999988079071044921875, 0.800000011920928955078125, 0.89999997615814208984375, 1.0 });
constant spvUnsafeArray<float, 41> _2391 = spvUnsafeArray<float, 41>({ 0.017999999225139617919921875, 0.02700000070035457611083984375, 0.0390000008046627044677734375, 0.056000001728534698486328125, 0.076999999582767486572265625, 0.104999996721744537353515625, 0.1410000026226043701171875, 0.185000002384185791015625, 0.23700000345706939697265625, 0.2980000078678131103515625, 0.3680000007152557373046875, 0.444999992847442626953125, 0.527000010013580322265625, 0.612999975681304931640625, 0.698000013828277587890625, 0.778999984264373779296875, 0.8519999980926513671875, 0.913999974727630615234375, 0.961000025272369384765625, 0.9900000095367431640625, 1.0, 0.9900000095367431640625, 0.961000025272369384765625, 0.913999974727630615234375, 0.8519999980926513671875, 0.778999984264373779296875, 0.698000013828277587890625, 0.612999975681304931640625, 0.527000010013580322265625, 0.444999992847442626953125, 0.3680000007152557373046875, 0.2980000078678131103515625, 0.23700000345706939697265625, 0.185000002384185791015625, 0.1410000026226043701171875, 0.104999996721744537353515625, 0.076999999582767486572265625, 0.056000001728534698486328125, 0.0390000008046627044677734375, 0.02700000070035457611083984375, 0.017999999225139617919921875 });
constant spvUnsafeArray<float, 21> _2405 = spvUnsafeArray<float, 21>({ 0.0199999995529651641845703125, 0.0390000008046627044677734375, 0.07400000095367431640625, 0.13500000536441802978515625, 0.236000001430511474609375, 0.3860000073909759521484375, 0.60699999332427978515625, 0.800000011920928955078125, 0.94599997997283935546875, 0.986000001430511474609375, 1.0, 0.986000001430511474609375, 0.94599997997283935546875, 0.800000011920928955078125, 0.60699999332427978515625, 0.3860000073909759521484375, 0.236000001430511474609375, 0.13500000536441802978515625, 0.07400000095367431640625, 0.0390000008046627044677734375, 0.0199999995529651641845703125 });

static inline __attribute__((always_inline))
int2 dispatch_origin(constant Renderer2DConstants& pc)
{
    return int2(int(pc.data.w & 65535u), int((pc.data.w >> 16u) & 65535u));
}

static inline __attribute__((always_inline))
float sd_box(thread const float2& p, thread const float2& halfSize)
{
    float2 q = abs(p) - halfSize;
    return length(fast::max(q, float2(0.0))) + fast::min(fast::max(q.x, q.y), 0.0);
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
    bool _1231 = outside.x == 0.0;
    bool _1238;
    if (!_1231)
    {
        _1238 = outside.y == 0.0;
    }
    else
    {
        _1238 = _1231;
    }
    float _1239;
    if (_1238)
    {
        _1239 = outside.x + outside.y;
    }
    else
    {
        _1239 = powr(powr(outside.x, cornerPower) + powr(outside.y, cornerPower), 1.0 / cornerPower);
    }
    float squircleNorm = _1239;
    return (mix(roundedNorm, squircleNorm, fast::clamp(amount, 0.0, 1.0)) + fast::min(fast::max(q.x, q.y), 0.0)) - safeRadius;
}

static inline __attribute__((always_inline))
float notch_flare_size(thread const float2& size, thread const float& amount, thread const float& depth)
{
    float progress = fast::clamp((fast::clamp(amount, 0.0, 1.0) - 0.85000002384185791015625) / 0.1500000059604644775390625, 0.0, 1.0);
    float _1582;
    if (depth > 0.5)
    {
        _1582 = depth;
    }
    else
    {
        _1582 = fast::clamp(size.y * 0.62000000476837158203125, 0.0, 92.0);
    }
    float depthTarget = _1582;
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
        float _1159;
        if (p.x < 0.0)
        {
            _1159 = radii.x;
        }
        else
        {
            _1159 = radii.y;
        }
        return _1159;
    }
    float _1172;
    if (p.x < 0.0)
    {
        _1172 = radii.w;
    }
    else
    {
        _1172 = radii.z;
    }
    return _1172;
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
    bool _1328 = outside.x == 0.0;
    bool _1335;
    if (!_1328)
    {
        _1335 = outside.y == 0.0;
    }
    else
    {
        _1335 = _1328;
    }
    float _1336;
    if (_1335)
    {
        _1336 = outside.x + outside.y;
    }
    else
    {
        _1336 = powr(powr(outside.x, cornerPower) + powr(outside.y, cornerPower), 1.0 / cornerPower);
    }
    float squircleNorm = _1336;
    return (mix(roundedNorm, squircleNorm, fast::clamp(amount, 0.0, 1.0)) + fast::min(fast::max(q.x, q.y), 0.0)) - safeRadius;
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
        float2 param = pixel - center;
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
float shape_sdf(thread const float2& pixel, constant Renderer2DConstants& pc)
{
    bool _1829 = (pc.data.y & 2097152u) != 0u;
    bool _1836;
    if (_1829)
    {
        _1836 = (pc.data.y & 4194304u) == 0u;
    }
    else
    {
        _1836 = _1829;
    }
    if (_1836)
    {
        float2 halfSize = fast::max(pc.rect.zw * 0.5, float2(0.5));
        float2 center = pc.rect.xy + halfSize;
        float2 param = pixel - center;
        float2 param_1 = halfSize;
        return sd_box(param, param_1);
    }
    float2 halfSize_1 = fast::max(pc.rect.zw * 0.5, float2(0.5));
    float2 center_1 = pc.rect.xy + halfSize_1;
    float2 p = pixel - center_1;
    if ((pc.data.x & 255u) == 3u)
    {
        float2 q = p / halfSize_1;
        return (length(q) - 1.0) * fast::min(halfSize_1.x, halfSize_1.y);
    }
    float _1900;
    if ((pc.data.x & 255u) == 1u)
    {
        _1900 = 0.0;
    }
    else
    {
        float _1908;
        if ((pc.data.y & 4194304u) != 0u)
        {
            _1908 = pc.effect0.y;
        }
        else
        {
            _1908 = pc.effect1.x;
        }
        _1900 = fast::clamp(_1908, 0.0, fast::min(halfSize_1.x, halfSize_1.y));
    }
    float radius = _1900;
    float squircleAmount = fast::clamp(pc.uvRect.x, 0.0, 1.0);
    float _1933;
    if (pc.uvRect.y <= 0.001000000047497451305389404296875)
    {
        _1933 = 4.0;
    }
    else
    {
        _1933 = pc.uvRect.y;
    }
    float squirclePower = fast::clamp(_1933, 2.0, 5.0);
    if ((pc.data.x & 255u) == 6u)
    {
        float2 param_2 = p;
        float2 param_3 = halfSize_1;
        float param_4 = radius;
        float param_5 = squircleAmount;
        float param_6 = squirclePower;
        return sd_squircle_box(param_2, param_3, param_4, param_5, param_6);
    }
    if ((pc.data.x & 255u) == 7u)
    {
        float2 param_7 = pixel;
        float4 param_8 = pc.rect;
        float param_9 = radius;
        float param_10 = squircleAmount;
        float param_11 = squirclePower;
        float param_12 = pc.uvRect.z;
        float param_13 = pc.uvRect.w;
        return sd_notched_squircle(param_7, param_8, param_9, param_10, param_11, param_12, param_13);
    }
    float2 param_14 = p;
    float2 param_15 = halfSize_1;
    float param_16 = radius;
    return sd_rounded_box(param_14, param_15, param_16);
}

static inline __attribute__((always_inline))
float shape_mask(thread const float2& pixel, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 2097152u) != 0u)
    {
        float2 param = pixel;
        return smoothstep(0.75, -0.75, shape_sdf(param, pc));
    }
    float _2012;
    if ((pc.data.y & 6291456u) != 0u)
    {
        _2012 = 0.0;
    }
    else
    {
        _2012 = fast::max(pc.effect1.z, 0.0);
    }
    float featherRadius = _2012;
    if (featherRadius > 0.001000000047497451305389404296875)
    {
        float2 param_1 = pixel;
        return 1.0 - smoothstep(0.0, featherRadius, shape_sdf(param_1, pc));
    }
    float softness = fast::max(pc.effect1.y, 0.75);
    float2 param_2 = pixel;
    return smoothstep(softness, -softness, shape_sdf(param_2, pc));
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
float inherited_shape_mask(thread const float2& pixel, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 2097152u) == 0u)
    {
        return 1.0;
    }
    uint primitive = uint(fast::max(pc.color2.x, 0.0) + 0.5);
    float2 halfSize = fast::max(pc.effect1.zw * 0.5, float2(0.5));
    float2 center = pc.effect1.xy + halfSize;
    float2 p = pixel - center;
    float2 param = p;
    float2 param_1 = halfSize;
    float sdf = sd_box(param, param_1);
    if (primitive == 3u)
    {
        float2 q = p / halfSize;
        sdf = (length(q) - 1.0) * fast::min(halfSize.x, halfSize.y);
    }
    else
    {
        if (primitive == 6u)
        {
            float radius = fast::clamp(pc.color2.y, 0.0, fast::min(halfSize.x, halfSize.y));
            float2 param_2 = p;
            float2 param_3 = halfSize;
            float param_4 = radius;
            float param_5 = pc.color2.z;
            float param_6 = pc.color2.w;
            sdf = sd_squircle_box(param_2, param_3, param_4, param_5, param_6);
        }
        else
        {
            if (primitive == 7u)
            {
                float radius_1 = fast::clamp(pc.color2.y, 0.0, fast::min(halfSize.x, halfSize.y));
                float4 maskParams = decode_notched_mask_params(pc);
                float2 param_7 = pixel;
                float4 param_8 = pc.effect1;
                float param_9 = radius_1;
                float param_10 = maskParams.x;
                float param_11 = maskParams.y;
                float param_12 = maskParams.z;
                float param_13 = maskParams.w;
                sdf = sd_notched_squircle(param_7, param_8, param_9, param_10, param_11, param_12, param_13);
            }
            else
            {
                if (primitive == 2u)
                {
                    float radius_2 = fast::clamp(pc.color2.y, 0.0, fast::min(halfSize.x, halfSize.y));
                    float2 param_14 = p;
                    float2 param_15 = halfSize;
                    float param_16 = radius_2;
                    sdf = sd_rounded_box(param_14, param_15, param_16);
                }
            }
        }
    }
    if ((pc.data.y & 536870912u) != 0u)
    {
        return smoothstep(0.0, 1.0, sdf);
    }
    if ((pc.data.y & 16384u) != 0u)
    {
        return float(sdf <= 0.0);
    }
    return smoothstep(1.0, -1.0, sdf);
}

static inline __attribute__((always_inline))
float gradient_t(thread const float2& localUv, constant Renderer2DConstants& pc)
{
    float2 _797;
    if ((pc.data.y & 2097152u) != 0u)
    {
        _797 = pc.uvRect.xy;
    }
    else
    {
        _797 = pc.color2.xy;
    }
    float2 start = _797;
    float2 _816;
    if ((pc.data.y & 2097152u) != 0u)
    {
        _816 = pc.uvRect.zw;
    }
    else
    {
        _816 = pc.color2.zw;
    }
    float2 end = _816;
    float2 axis = end - start;
    float len2 = dot(axis, axis);
    if (len2 <= 9.9999997473787516355514526367188e-05)
    {
        return 0.0;
    }
    if ((pc.data.y & 2048u) != 0u)
    {
        return fast::clamp(length(localUv - start) / sqrt(len2), 0.0, 1.0);
    }
    return fast::clamp(dot(localUv - start, axis) / len2, 0.0, 1.0);
}

static inline __attribute__((always_inline))
float blur_gradient_alpha_factor(thread const float2& pixel, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 1u) == 0u)
    {
        return 1.0;
    }
    float2 localUv = (pixel - pc.rect.xy) / fast::max(pc.rect.zw, float2(1.0));
    float2 param = localUv;
    float alpha = mix(pc.color0.w, pc.color1.w, gradient_t(param, pc));
    if ((pc.data.y & 1048576u) != 0u)
    {
        return fast::clamp(alpha, 0.0, 1.0);
    }
    float maxAlpha = fast::max(fast::max(pc.color0.w, pc.color1.w), 9.9999997473787516355514526367188e-05);
    return fast::clamp(alpha / maxAlpha, 0.0, 1.0);
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

static inline __attribute__((always_inline))
uint blur_pass_index(constant Renderer2DConstants& pc)
{
    uint _2071;
    if ((pc.data.y & 4194304u) != 0u)
    {
        _2071 = 0u;
    }
    else
    {
        _2071 = pc.data.z & 65535u;
    }
    return _2071;
}

static inline __attribute__((always_inline))
float2 blur_primary_axis(thread const uint& passIndex)
{
    uint mode = passIndex % 3u;
    if (mode == 1u)
    {
        return float2(0.707106769084930419921875);
    }
    if (mode == 2u)
    {
        return float2(0.707106769084930419921875, -0.707106769084930419921875);
    }
    return float2(1.0, 0.0);
}

static inline __attribute__((always_inline))
float2 blur_axis(thread const uint& passIndex, thread const bool& perpendicular)
{
    uint param = passIndex;
    float2 axis = blur_primary_axis(param);
    float2 _2106;
    if (perpendicular)
    {
        _2106 = float2(-axis.y, axis.x);
    }
    else
    {
        _2106 = axis;
    }
    return _2106;
}

static inline __attribute__((always_inline))
float4 source_color(thread const int2& pixel, constant Renderer2DConstants& pc, texture2d<float, access::read_write> sceneColorBuffer, texture2d<float> uiStaticSurface, texture2d<float> uiExternalBackdropSurface)
{
    spvImageFence(sceneColorBuffer);
    float4 source = sceneColorBuffer.read(uint2(pixel));
    if ((pc.data.z & 2147483648u) != 0u)
    {
        float4 param = uiStaticSurface.read(uint2(pixel));
        float4 param_1 = source;
        source = blend_over(param, param_1);
    }
    if ((pc.data.z & 1073741824u) != 0u)
    {
        float4 param_2 = uiExternalBackdropSurface.read(uint2(pixel));
        float4 param_3 = source;
        source = blend_over(param_2, param_3);
    }
    bool _1005 = (pc.data.y & 4194304u) != 0u;
    bool _1013;
    if (_1005)
    {
        _1013 = (pc.data.z & 536870912u) != 0u;
    }
    else
    {
        _1013 = _1005;
    }
    if (_1013)
    {
        float2 p = float2(pixel) + float2(0.5);
        bool _1027 = all(p >= pc.color0.xy);
        bool _1040;
        if (_1027)
        {
            _1040 = all(p <= (pc.color0.xy + pc.color0.zw));
        }
        else
        {
            _1040 = _1027;
        }
        if (_1040)
        {
            float y = (p.y - pc.color0.y) / fast::max(pc.color0.w, 1.0);
            float t = fast::clamp((y - pc.color1.z) / fast::max(pc.color1.w - pc.color1.z, 9.9999997473787516355514526367188e-05), 0.0, 1.0);
            float alpha = fast::clamp(mix(pc.color1.x, pc.color1.y, t), 0.0, 1.0);
            float3 capColour = float3(float((pc.data.x >> 8u) & 255u), float((pc.data.x >> 16u) & 255u), float((pc.data.x >> 24u) & 255u)) / float3(255.0);
            float4 param_4 = float4(capColour, alpha);
            float4 param_5 = source;
            source = blend_over(param_4, param_5);
        }
    }
    return source;
}

static inline __attribute__((always_inline))
float4 blur_input_color(thread const int2& pixel, thread const bool& readScratch, constant Renderer2DConstants& pc, texture2d<float, access::read_write> sceneColorBuffer, texture2d<float> uiStaticSurface, texture2d<float> uiExternalBackdropSurface, texture2d<float, access::read_write> tempSurface)
{
    float4 _2121;
    if (readScratch)
    {
        spvImageFence(tempSurface);
        _2121 = tempSurface.read(uint2(pixel));
    }
    else
    {
        int2 param = pixel;
        _2121 = source_color(param, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface);
    }
    return _2121;
}

static inline __attribute__((always_inline))
float4 blur_input_color_at(thread const float2& coord, thread const bool& readScratch, constant Renderer2DConstants& pc, texture2d<float, access::read_write> sceneColorBuffer, texture2d<float> uiStaticSurface, texture2d<float> uiExternalBackdropSurface, texture2d<float, access::read_write> tempSurface, texture2d<float, access::read_write> uiBlurSurface)
{
    int2 screenSize = int2(uiBlurSurface.get_width(), uiBlurSurface.get_height());
    float2 clamped = fast::clamp(coord, float2(0.0), float2(screenSize - int2(1)));
    if ((pc.data.y & 16777216u) != 0u)
    {
        float2 lo = fast::clamp(ceil(pc.uvRect.xy), float2(0.0), float2(screenSize - int2(1)));
        float2 hi = fast::clamp(floor(pc.uvRect.xy + pc.uvRect.zw) - float2(1.0), lo, float2(screenSize - int2(1)));
        clamped = fast::clamp(clamped, lo, hi);
    }
    int2 p0 = int2(floor(clamped));
    int2 p1 = min((p0 + int2(1)), (screenSize - int2(1)));
    float2 f = fract(clamped);
    int2 param = p0;
    bool param_1 = readScratch;
    float4 c00 = blur_input_color(param, param_1, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface);
    if (f.x == 0.0)
    {
        if (f.y == 0.0)
        {
            return c00;
        }
        int2 param_2 = int2(p0.x, p1.y);
        bool param_3 = readScratch;
        return mix(c00, blur_input_color(param_2, param_3, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface), float4(f.y));
    }
    int2 param_4 = int2(p1.x, p0.y);
    bool param_5 = readScratch;
    float4 c10 = blur_input_color(param_4, param_5, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface);
    if (f.y == 0.0)
    {
        return mix(c00, c10, float4(f.x));
    }
    int2 param_6 = int2(p0.x, p1.y);
    bool param_7 = readScratch;
    float4 c01 = blur_input_color(param_6, param_7, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface);
    int2 param_8 = p1;
    bool param_9 = readScratch;
    float4 c11 = blur_input_color(param_8, param_9, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface);
    return mix(mix(c00, c10, float4(f.x)), mix(c01, c11, float4(f.x)), float4(f.y));
}

static inline __attribute__((always_inline))
float4 sample_axis_blur_at(thread const float2& pixelCoord, thread const float2& axis, thread const float& radius, thread const bool& readScratch, constant Renderer2DConstants& pc, texture2d<float, access::read_write> sceneColorBuffer, texture2d<float> uiStaticSurface, texture2d<float> uiExternalBackdropSurface, texture2d<float, access::read_write> tempSurface, texture2d<float, access::read_write> uiBlurSurface)
{
    if (radius <= 0.5)
    {
        float2 param = pixelCoord;
        bool param_1 = readScratch;
        return blur_input_color_at(param, param_1, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
    }
    bool highQualityForeground = (pc.data.y & 67108864u) != 0u;
    int activeSampleCount = highQualityForeground ? 41 : 21;
    float3 weightedRgb = float3(0.0);
    float alphaSum = 0.0;
    float weightSum = 0.0;
    float _2320;
    float _2368;
    for (int i = 0; i < activeSampleCount; i++)
    {
        if (highQualityForeground)
        {
            _2320 = _2350[i];
        }
        else
        {
            _2320 = _2359[i];
        }
        float offset = _2320;
        if (highQualityForeground)
        {
            _2368 = _2391[i];
        }
        else
        {
            _2368 = _2405[i];
        }
        float sampleWeight = _2368;
        float2 sampleCoord = pixelCoord + ((axis * offset) * radius);
        float2 param_2 = sampleCoord;
        bool param_3 = readScratch;
        float4 sampleColor = blur_input_color_at(param_2, param_3, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
        weightedRgb += ((sampleColor.xyz * sampleColor.w) * sampleWeight);
        alphaSum += (sampleColor.w * sampleWeight);
        weightSum += sampleWeight;
    }
    float _2448;
    if (weightSum > 9.9999997473787516355514526367188e-05)
    {
        _2448 = alphaSum / weightSum;
    }
    else
    {
        _2448 = 0.0;
    }
    float sourceAlpha = _2448;
    float3 _2459;
    if (alphaSum > 9.9999997473787516355514526367188e-05)
    {
        _2459 = weightedRgb / float3(alphaSum);
    }
    else
    {
        _2459 = float3(0.0);
    }
    float3 sourceRgb = _2459;
    return float4(sourceRgb, sourceAlpha);
}

static inline __attribute__((always_inline))
float4 sample_axis_blur(thread const int2& pixel, thread const float2& axis, thread const float& radius, thread const bool& readScratch, constant Renderer2DConstants& pc, texture2d<float, access::read_write> sceneColorBuffer, texture2d<float> uiStaticSurface, texture2d<float> uiExternalBackdropSurface, texture2d<float, access::read_write> tempSurface, texture2d<float, access::read_write> uiBlurSurface)
{
    float2 param = float2(pixel);
    float2 param_1 = axis;
    float param_2 = radius;
    bool param_3 = readScratch;
    return sample_axis_blur_at(param, param_1, param_2, param_3, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
}

static inline __attribute__((always_inline))
float primitive_bounds_padding(constant Renderer2DConstants& pc)
{
    if ((pc.data.x & 255u) != 7u)
    {
        return 0.0;
    }
    float2 param = fast::max(pc.rect.zw, float2(1.0));
    float param_1 = pc.uvRect.z;
    float param_2 = pc.uvRect.w;
    return notch_flare_size(param, param_1, param_2) + 6.0;
}

static inline __attribute__((always_inline))
float glass_clamp(thread const float& value, thread const float& low, thread const float& high)
{
    return fast::min(fast::max(value, low), high);
}

static inline __attribute__((always_inline))
float glass_body_blend(thread const float& width, thread const float& height)
{
    float param = (fast::min(width, height) - 48.0) / 92.0;
    float param_1 = 0.0;
    float param_2 = 1.0;
    float t = glass_clamp(param, param_1, param_2);
    return (t * t) * (3.0 - (2.0 * t));
}

static inline __attribute__((always_inline))
float glass_optical_diameter(thread const float& width, thread const float& height, thread const float& radius)
{
    float shortSide = fast::min(width, height);
    float _268;
    if (radius < 0.0)
    {
        _268 = shortSide;
    }
    else
    {
        _268 = 2.0 * radius;
    }
    float diameter = _268;
    float param = diameter;
    float param_1 = shortSide * 0.20000000298023223876953125;
    float param_2 = shortSide;
    return glass_clamp(param, param_1, param_2);
}

static inline __attribute__((always_inline))
float glass_edge_thickness(thread const float& width, thread const float& height, thread const float& radius)
{
    float param = width;
    float param_1 = height;
    float body = glass_body_blend(param, param_1);
    float shortSide = fast::min(width, height);
    float param_2 = width;
    float param_3 = height;
    float param_4 = radius;
    float diameter = glass_optical_diameter(param_2, param_3, param_4);
    float opticalSize = shortSide + ((diameter - shortSide) * body);
    return fast::max(opticalSize * (0.2199999988079071044921875 - (0.100000001490116119384765625 * body)), 3.0);
}

static inline __attribute__((always_inline))
float glass_lens_band(thread const float& _distance, thread const float& edge, thread const float& width, thread const float& height, thread const float& normalX)
{
    float param = width;
    float param_1 = height;
    float bevelWidth = edge * (1.0 + (((3.0 * glass_body_blend(param, param_1)) * normalX) * normalX));
    float param_2 = 1.0 + (_distance / fast::max(bevelWidth, 1.0));
    float param_3 = 0.0;
    float param_4 = 1.0;
    return glass_clamp(param_2, param_3, param_4);
}

static inline __attribute__((always_inline))
float glass_refraction_strength(thread const float& width, thread const float& height, thread float& amount, thread const float& radius)
{
    float param = amount;
    float param_1 = 0.0;
    float param_2 = 1.0;
    amount = glass_clamp(param, param_1, param_2);
    float shortSide = fast::min(width, height);
    float param_3 = width / 500.0;
    float param_4 = 0.3499999940395355224609375;
    float param_5 = 2.0;
    float size = glass_clamp(param_3, param_4, param_5);
    float thin = fast::min((amount * 56.0) * size, shortSide * 0.4000000059604644775390625);
    float remaining = 1.0 - amount;
    float param_6 = width;
    float param_7 = height;
    float param_8 = radius;
    float body = (glass_optical_diameter(param_6, param_7, param_8) * 0.4000000059604644775390625) * (1.0 - ((remaining * remaining) * remaining));
    float param_9 = width;
    float param_10 = height;
    return thin + ((body - thin) * glass_body_blend(param_9, param_10));
}

static inline __attribute__((always_inline))
void glass_lens_offset(thread const float& _distance, thread const float& edge, thread const float& strength, thread const float& normalX, thread const float& normalY, thread const float& localX, thread const float& localY, thread const float& width, thread const float& height, thread float& offsetX, thread float& offsetY)
{
    float param = _distance;
    float param_1 = edge;
    float param_2 = width;
    float param_3 = height;
    float param_4 = normalX;
    float band = glass_lens_band(param, param_1, param_2, param_3, param_4);
    offsetY = 0.0;
    offsetX = 0.0;
    if ((band <= 0.0) || (strength <= 0.0))
    {
        return;
    }
    float curve = (band * sqrt(band)) / (1.0 + (0.800000011920928955078125 * sqrt(fast::max(1.0 - band, 9.9999997473787516355514526367188e-05))));
    float thinX = (-normalX) * curve;
    float thinY = (-normalY) * curve;
    float param_5 = width;
    float param_6 = height;
    float body = glass_body_blend(param_5, param_6);
    if (body <= 0.0)
    {
        offsetX = thinX * strength;
        offsetY = thinY * strength;
        return;
    }
    float param_7 = ((localX * 2.0) / fast::max(width, 1.0)) - 1.0;
    float param_8 = -1.0;
    float param_9 = 1.0;
    float u = glass_clamp(param_7, param_8, param_9);
    float param_10 = ((localY * 2.0) / fast::max(height, 1.0)) - 1.0;
    float param_11 = -1.0;
    float param_12 = 1.0;
    float v = glass_clamp(param_10, param_11, param_12);
    float shoulder = band * (2.0 - band);
    float bevel = band;
    float nx = (normalX * bevel) + ((u * 0.23999999463558197021484375) * shoulder);
    float ny = (normalY * bevel) + ((v * 0.23999999463558197021484375) * shoulder);
    float nz = sqrt(fast::max(1.0 - (bevel * bevel), 9.9999997473787516355514526367188e-05));
    float inverseNormal = 1.0 / sqrt(((nx * nx) + (ny * ny)) + (nz * nz));
    nx *= inverseNormal;
    ny *= inverseNormal;
    nz *= inverseNormal;
    float index = 1.4500000476837158203125;
    float eta = 1.0 / index;
    float transmission = (eta * nz) - sqrt(fast::max(1.0 - ((eta * eta) * (1.0 - (nz * nz))), 9.9999997473787516355514526367188e-05));
    float rayX = (transmission * nx) * index;
    float rayY = (transmission * ny) * index;
    float rayLength = sqrt((rayX * rayX) + (rayY * rayY));
    float exitCosine = sqrt(fast::max(1.0 - (rayLength * rayLength), 0.01439999975264072418212890625));
    float projection = rayLength / exitCosine;
    float foldCurve = (band * band) / (0.20000000298023223876953125 + (0.800000011920928955078125 * band));
    float travel = foldCurve * (0.85000002384185791015625 + (0.1500000059604644775390625 * fast::min((1.07000005245208740234375 * projection) / (0.550000011920928955078125 + projection), 1.0)));
    float thickness = (1.0 - ((0.14000000059604644775390625 * u) * u)) - ((0.039999999105930328369140625 * v) * v);
    float rayScale = (travel * thickness) / fast::max(rayLength, 9.9999997473787516355514526367188e-05);
    offsetX = (thinX + (((rayX * rayScale) - thinX) * body)) * strength;
    offsetY = (thinY + (((rayY * rayScale) - thinY) * body)) * strength;
}

static inline __attribute__((always_inline))
float glass_lens_footprint(thread const float& _distance, thread const float& edge, thread const float& strength, thread const float& normalX, thread const float& normalY, thread const float& localX, thread const float& localY, thread const float& width, thread const float& height)
{
    float param = _distance + 0.5;
    float param_1 = edge;
    float param_2 = strength;
    float param_3 = normalX;
    float param_4 = normalY;
    float param_5 = localX + (normalX * 0.5);
    float param_6 = localY + (normalY * 0.5);
    float param_7 = width;
    float param_8 = height;
    float param_9;
    float param_10;
    glass_lens_offset(param, param_1, param_2, param_3, param_4, param_5, param_6, param_7, param_8, param_9, param_10);
    float forwardX = param_9;
    float forwardY = param_10;
    float param_11 = _distance - 0.5;
    float param_12 = edge;
    float param_13 = strength;
    float param_14 = normalX;
    float param_15 = normalY;
    float param_16 = localX - (normalX * 0.5);
    float param_17 = localY - (normalY * 0.5);
    float param_18 = width;
    float param_19 = height;
    float param_20;
    float param_21;
    glass_lens_offset(param_11, param_12, param_13, param_14, param_15, param_16, param_17, param_18, param_19, param_20, param_21);
    float backwardX = param_20;
    float backwardY = param_21;
    float dx = (normalX + forwardX) - backwardX;
    float dy = (normalY + forwardY) - backwardY;
    float param_22 = sqrt((dx * dx) + (dy * dy));
    float param_23 = 1.0;
    float param_24 = 4.0;
    return glass_clamp(param_22, param_23, param_24);
}

static inline __attribute__((always_inline))
float4 liquid_backdrop(thread const float2& point, thread const float& radius, thread const float& diffusion, thread const bool& painted, thread const bool& preserveColour, thread const float2& normal, thread const float& footprint, constant Renderer2DConstants& pc, texture2d<float, access::read_write> sceneColorBuffer, texture2d<float> uiStaticSurface, texture2d<float> uiExternalBackdropSurface, texture2d<float, access::read_write> tempSurface, texture2d<float, access::read_write> uiBlurSurface)
{
    if ((!painted) && (diffusion <= 0.0))
    {
        float2 param = point;
        bool param_1 = false;
        return blur_input_color_at(param, param_1, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
    }
    float2 param_2 = point;
    float2 param_3 = float2(0.0, 1.0);
    float param_4 = radius;
    bool param_5 = true;
    float4 wide = sample_axis_blur_at(param_2, param_3, param_4, param_5, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
    if (painted || (diffusion >= 0.999000012874603271484375))
    {
        return wide;
    }
    float2 n = (length(normal) > 0.100000001490116119384765625) ? normal : float2(0.0, 1.0);
    float2 across = n * (0.5 * footprint);
    float2 along = float2(-n.y, n.x) * 0.5;
    float2 param_6 = (point + across) + along;
    bool param_7 = false;
    float2 param_8 = (point + across) - along;
    bool param_9 = false;
    float2 param_10 = (point - across) + along;
    bool param_11 = false;
    float2 param_12 = (point - across) - along;
    bool param_13 = false;
    float4 detail = (((blur_input_color_at(param_6, param_7, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface) + blur_input_color_at(param_8, param_9, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface)) + blur_input_color_at(param_10, param_11, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface)) + blur_input_color_at(param_12, param_13, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface)) * 0.25;
    if (!preserveColour)
    {
        float4 _2633 = detail;
        float4 _2635 = detail;
        float3 _2641 = mix(_2633.xyz, fast::min(_2635.xyz, wide.xyz), float3(0.699999988079071044921875));
        detail.x = _2641.x;
        detail.y = _2641.y;
        detail.z = _2641.z;
    }
    return mix(detail, wide, float4(diffusion));
}

static inline __attribute__((always_inline))
float glass_dispersion_strength(thread const float& amount)
{
    float param = amount;
    float param_1 = 0.0;
    float param_2 = 1.0;
    return glass_clamp(param, param_1, param_2) * 0.64999997615814208984375;
}

static inline __attribute__((always_inline))
float3 liquid_tone(thread const float3& colour)
{
    float luminance = dot(colour, float3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875));
    float bounded = fast::clamp(luminance, 0.0, 1.0);
    float mapped = (0.125 + (bounded * (0.9900000095367431640625 - (0.38999998569488525390625 * bounded)))) + (fast::max(luminance - 1.0, 0.0) * 0.20999999344348907470703125);
    if (mapped < luminance)
    {
        return colour * (mapped / fast::max(luminance, 9.9999997473787516355514526367188e-05));
    }
    return colour + (fast::max(float3(1.0) - colour, float3(0.0)) * ((mapped - luminance) / fast::max(1.0 - luminance, 9.9999997473787516355514526367188e-05)));
}

kernel void blur_2d_comp(constant Renderer2DConstants& pc [[buffer(0)]], texture2d<float, access::read_write> sceneColorBuffer [[texture(0)]], texture2d<float> uiStaticSurface [[texture(1)]], texture2d<float> uiExternalBackdropSurface [[texture(2)]], texture2d<float, access::read_write> tempSurface [[texture(3)]], texture2d<float, access::read_write> uiBlurSurface [[texture(4)]], uint3 gl_NumWorkGroups [[threadgroups_per_grid]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    uint tail = pc.data.y >> 1u;
    uint2 exactExtent = (gl_NumWorkGroups.xy * uint2(8u)) - uint2(tail & 7u, (tail >> 3u) & 7u);
    if (any(gl_GlobalInvocationID.xy >= exactExtent))
    {
        return;
    }
    int2 pixel = dispatch_origin(pc) + int2(gl_GlobalInvocationID.xy);
    int2 screenSize = int2(uiBlurSurface.get_width(), uiBlurSurface.get_height());
    bool _2696 = pixel.x < 0;
    bool _2703;
    if (!_2696)
    {
        _2703 = pixel.y < 0;
    }
    else
    {
        _2703 = _2696;
    }
    bool _2712;
    if (!_2703)
    {
        _2712 = pixel.x >= screenSize.x;
    }
    else
    {
        _2712 = _2703;
    }
    bool _2721;
    if (!_2712)
    {
        _2721 = pixel.y >= screenSize.y;
    }
    else
    {
        _2721 = _2712;
    }
    if (_2721)
    {
        return;
    }
    if ((pc.data.y & 128u) != 0u)
    {
        uiBlurSurface.write(float4(0.0), uint2(pixel));
        return;
    }
    if ((pc.data.y & 512u) != 0u)
    {
        bool _2746 = (pc.data.y & 16777216u) != 0u;
        bool _2770;
        if (_2746)
        {
            bool _2755 = pixel.x < int(ceil(pc.uvRect.x));
            bool _2769;
            if (!_2755)
            {
                _2769 = pixel.x >= int(floor(pc.uvRect.x + pc.uvRect.z));
            }
            else
            {
                _2769 = _2755;
            }
            _2770 = _2769;
        }
        else
        {
            _2770 = _2746;
        }
        if (_2770)
        {
            return;
        }
        spvImageFence(uiBlurSurface);
        float4 src = uiBlurSurface.read(uint2(pixel));
        if (pc.effect0.z > 0.5)
        {
            float2 p = float2(pixel) + float2(0.5);
            float2 param = p;
            float2 param_1 = p;
            float2 param_2 = p;
            float coverage = fast::clamp(((shape_mask(param, pc) * inherited_shape_mask(param_1, pc)) * pc.effect0.w) * blur_gradient_alpha_factor(param_2, pc), 0.0, 1.0);
            if (coverage <= 0.001000000047497451305389404296875)
            {
                return;
            }
            spvImageFence(sceneColorBuffer);
            float4 dst = sceneColorBuffer.read(uint2(pixel));
            if ((pc.data.y & 33554432u) != 0u)
            {
                sceneColorBuffer.write(float4(mix(dst.xyz, src.xyz, float3(coverage)), dst.w), uint2(pixel));
                return;
            }
            float alpha = mix(dst.w, src.w, coverage);
            float3 premultiplied = mix(dst.xyz * dst.w, src.xyz * src.w, float3(coverage));
            float3 _2860;
            if (alpha > 9.9999997473787516355514526367188e-05)
            {
                _2860 = premultiplied / float3(alpha);
            }
            else
            {
                _2860 = float3(0.0);
            }
            sceneColorBuffer.write(float4(_2860, alpha), uint2(pixel));
            return;
        }
        if (src.w <= 0.001000000047497451305389404296875)
        {
            return;
        }
        spvImageFence(sceneColorBuffer);
        float4 dst_1 = sceneColorBuffer.read(uint2(pixel));
        float4 param_3 = dst_1;
        float4 param_4 = src;
        sceneColorBuffer.write(blend_over(param_3, param_4), uint2(pixel));
        return;
    }
    float radius = fast::clamp(pc.effect0.x, 0.0, 48.0);
    float diffusionRadius = radius;
    bool liquidMaterial = (pc.data.y & 4194304u) != 0u;
    bool paintedMaterial = (pc.data.y & 524288u) != 0u;
    if ((liquidMaterial && (!paintedMaterial)) && (diffusionRadius > 0.5))
    {
        radius = fast::max(radius, fast::min(fast::min(pc.rect.z, pc.rect.w) * 0.5, 28.5));
    }
    uint passIndex = blur_pass_index(pc);
    if ((pc.data.y & 131072u) != 0u)
    {
        uint param_5 = passIndex;
        bool param_6 = false;
        int2 param_7 = pixel;
        float2 param_8 = blur_axis(param_5, param_6);
        float param_9 = radius;
        bool param_10 = false;
        float4 blurred = sample_axis_blur(param_7, param_8, param_9, param_10, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
        tempSurface.write(blurred, uint2(pixel));
        return;
    }
    if ((pc.data.y & 262144u) == 0u)
    {
        return;
    }
    if ((pc.data.y & 67108864u) != 0u)
    {
        uint param_11 = passIndex;
        bool param_12 = true;
        int2 param_13 = pixel;
        float2 param_14 = blur_axis(param_11, param_12);
        float param_15 = radius;
        bool param_16 = true;
        float4 blurred_1 = sample_axis_blur(param_13, param_14, param_15, param_16, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
        uiBlurSurface.write(float4(blurred_1.xyz, fast::clamp(blurred_1.w * pc.effect0.w, 0.0, 1.0)), uint2(pixel));
        return;
    }
    float2 p_1 = float2(pixel) + float2(0.5);
    float _3004;
    if ((pc.data.y & 4194304u) != 0u)
    {
        _3004 = 0.0;
    }
    else
    {
        _3004 = fast::max(pc.effect1.z, 0.0);
    }
    float featherRadius = _3004;
    float boundsPad = featherRadius + primitive_bounds_padding(pc);
    bool _3022 = p_1.x < (pc.rect.x - boundsPad);
    bool _3033;
    if (!_3022)
    {
        _3033 = p_1.y < (pc.rect.y - boundsPad);
    }
    else
    {
        _3033 = _3022;
    }
    bool _3047;
    if (!_3033)
    {
        _3047 = p_1.x > ((pc.rect.x + pc.rect.z) + boundsPad);
    }
    else
    {
        _3047 = _3033;
    }
    bool _3061;
    if (!_3047)
    {
        _3061 = p_1.y > ((pc.rect.y + pc.rect.w) + boundsPad);
    }
    else
    {
        _3061 = _3047;
    }
    if (_3061)
    {
        return;
    }
    float2 param_17 = p_1;
    float mask = shape_mask(param_17, pc);
    float2 param_18 = p_1;
    mask *= inherited_shape_mask(param_18, pc);
    if (mask <= 0.001000000047497451305389404296875)
    {
        return;
    }
    float4 source;
    if ((pc.data.y & 4194304u) != 0u)
    {
        float2 param_19 = p_1;
        float _distance = shape_sdf(param_19, pc);
        float shortSide = fast::min(pc.rect.z, pc.rect.w);
        bool paintedGlass = (pc.data.y & 524288u) != 0u;
        bool preserveColour = (pc.data.y & 268435456u) != 0u;
        float param_20 = pc.rect.z;
        float param_21 = pc.rect.w;
        float param_22 = pc.effect0.y;
        float edge = glass_edge_thickness(param_20, param_21, param_22);
        float rimWidth = fast::clamp(shortSide / 96.0, 0.75, 1.5);
        float2 normal = float2(0.0);
        float param_23 = _distance;
        float param_24 = edge;
        float param_25 = pc.rect.z;
        float param_26 = pc.rect.w;
        float param_27 = 1.0;
        bool _3136 = glass_lens_band(param_23, param_24, param_25, param_26, param_27) > 0.0;
        bool _3145;
        if (!_3136)
        {
            _3145 = _distance > ((-5.0) * rimWidth);
        }
        else
        {
            _3145 = _3136;
        }
        if (_3145)
        {
            float2 param_28 = p_1 + float2(0.5, 0.0);
            float2 param_29 = p_1 - float2(0.5, 0.0);
            float2 param_30 = p_1 + float2(0.0, 0.5);
            float2 param_31 = p_1 - float2(0.0, 0.5);
            normal = float2(shape_sdf(param_28, pc) - shape_sdf(param_29, pc), shape_sdf(param_30, pc) - shape_sdf(param_31, pc));
            normal /= float2(fast::max(length(normal), 9.9999997473787516355514526367188e-05));
        }
        float param_32 = _distance;
        float param_33 = edge;
        float param_34 = pc.rect.z;
        float param_35 = pc.rect.w;
        float param_36 = normal.x;
        float band = glass_lens_band(param_32, param_33, param_34, param_35, param_36);
        float strength = float(pc.data.z & 127u) / 127.0;
        float param_37 = pc.rect.z;
        float param_38 = pc.rect.w;
        float param_39 = strength;
        float param_40 = pc.effect0.y;
        float _3210 = glass_refraction_strength(param_37, param_38, param_39, param_40);
        float bend = _3210;
        if (paintedGlass)
        {
            bend *= 0.1500000059604644775390625;
        }
        float2 local = p_1 - pc.rect.xy;
        float param_41 = _distance;
        float param_42 = edge;
        float param_43 = bend;
        float param_44 = normal.x;
        float param_45 = normal.y;
        float param_46 = local.x;
        float param_47 = local.y;
        float param_48 = pc.rect.z;
        float param_49 = pc.rect.w;
        float param_50;
        float param_51;
        glass_lens_offset(param_41, param_42, param_43, param_44, param_45, param_46, param_47, param_48, param_49, param_50, param_51);
        float2 lensOffset;
        lensOffset.x = param_50;
        lensOffset.y = param_51;
        float2 samplePoint = float2(pixel) + lensOffset;
        float2 _3264;
        if (dot(lensOffset, lensOffset) > 9.9999997473787516355514526367188e-05)
        {
            _3264 = -fast::normalize(lensOffset);
        }
        else
        {
            _3264 = normal;
        }
        float2 opticalNormal = _3264;
        float _3279;
        if ((diffusionRadius > 0.0) && (band > 0.0))
        {
            float param_52 = _distance;
            float param_53 = edge;
            float param_54 = bend;
            float param_55 = normal.x;
            float param_56 = normal.y;
            float param_57 = local.x;
            float param_58 = local.y;
            float param_59 = pc.rect.z;
            float param_60 = pc.rect.w;
            _3279 = glass_lens_footprint(param_52, param_53, param_54, param_55, param_56, param_57, param_58, param_59, param_60);
        }
        else
        {
            _3279 = 1.0;
        }
        float footprint = _3279;
        float diffusion = smoothstep(0.5, 18.0, diffusionRadius);
        bool opticalTone = (strength > 0.0) || (diffusionRadius > 0.5);
        float4 _3325;
        if ((!opticalTone) && (!paintedGlass))
        {
            float2 param_61 = samplePoint;
            bool param_62 = false;
            _3325 = blur_input_color_at(param_61, param_62, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
        }
        else
        {
            float2 param_63 = samplePoint;
            float param_64 = radius;
            float param_65 = diffusion;
            bool param_66 = paintedGlass;
            bool param_67 = preserveColour;
            float2 param_68 = opticalNormal;
            float param_69 = footprint;
            _3325 = liquid_backdrop(param_63, param_64, param_65, param_66, param_67, param_68, param_69, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
        }
        source = _3325;
        float param_70 = float((pc.data.z >> 7u) & 127u) / 127.0;
        float dispersion = (glass_dispersion_strength(param_70) * band) * (1.0 - (diffusion * 0.75));
        if (dispersion > 0.0)
        {
            float2 param_71 = samplePoint + (opticalNormal * dispersion);
            float param_72 = radius;
            float param_73 = diffusion;
            bool param_74 = paintedGlass;
            bool param_75 = preserveColour;
            float2 param_76 = opticalNormal;
            float param_77 = footprint;
            source.x = liquid_backdrop(param_71, param_72, param_73, param_74, param_75, param_76, param_77, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface).x;
            float2 param_78 = samplePoint - (opticalNormal * dispersion);
            float param_79 = radius;
            float param_80 = diffusion;
            bool param_81 = paintedGlass;
            bool param_82 = preserveColour;
            float2 param_83 = opticalNormal;
            float param_84 = footprint;
            source.z = liquid_backdrop(param_78, param_79, param_80, param_81, param_82, param_83, param_84, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface).z;
        }
        if (((!paintedGlass) && (!preserveColour)) && opticalTone)
        {
            float3 param_85 = source.xyz;
            float3 _3422 = liquid_tone(param_85);
            source.x = _3422.x;
            source.y = _3422.y;
            source.z = _3422.z;
        }
        if (_distance > ((-5.0) * rimWidth))
        {
            float phase = (float((pc.data.z >> 21u) & 255u) / 255.0) * 6.283185482025146484375;
            float _3450;
            if ((pc.data.y & 8192u) != 0u)
            {
                _3450 = 1.0;
            }
            else
            {
                _3450 = 0.699999988079071044921875 + (0.300000011920928955078125 * cos((phase + (((p_1.x - pc.rect.x) / fast::max(pc.rect.z, 1.0)) * 3.1415927410125732421875)) + (normal.y * 0.800000011920928955078125)));
            }
            float light = _3450;
            float rim = float((pc.data.z >> 14u) & 127u) / 127.0;
            float vertical = powr(abs(normal.y), 3.5);
            float edgeDistance = _distance / rimWidth;
            float coreDistance = edgeDistance + (paintedGlass ? 1.14999997615814208984375 : 0.3499999940395355224609375);
            float core = exp(((-coreDistance) * coreDistance) / 0.550000011920928955078125);
            float soft = exp(((-(edgeDistance + 1.39999997615814208984375)) * (edgeDistance + 1.39999997615814208984375)) / 3.0);
            float highlight = ((rim * ((0.17000000178813934326171875 * core) + (0.017999999225139617919921875 * soft))) * vertical) * light;
            if (paintedGlass)
            {
                highlight = (rim * mix(0.0350000001490116119384765625 * soft, (0.560000002384185791015625 * core) + (0.07999999821186065673828125 * soft), vertical)) * light;
            }
            if (paintedGlass)
            {
                highlight *= 0.800000011920928955078125;
            }
            float shade = (0.319999992847442626953125 * powr(abs(normal.x), 3.0)) * exp(((-(edgeDistance + 0.1500000059604644775390625)) * (edgeDistance + 0.1500000059604644775390625)) / 0.4000000059604644775390625);
            if (paintedGlass)
            {
                shade = (0.3499999940395355224609375 * exp(((-(edgeDistance + 0.1500000059604644775390625)) * (edgeDistance + 0.1500000059604644775390625)) / 0.4000000059604644775390625)) + ((0.070000000298023223876953125 * soft) * (1.0 - vertical));
            }
            float4 _3592 = source;
            float3 _3594 = _3592.xyz * (1.0 - (rim * shade));
            source.x = _3594.x;
            source.y = _3594.y;
            source.z = _3594.z;
            if (highlight > 0.001000000047497451305389404296875)
            {
                float reach = fast::min(shortSide * 1.60000002384185791015625, 120.0);
                float spread = fast::min(shortSide * 0.4000000059604644775390625, 24.0);
                float2 tangent = float2(-normal.y, normal.x);
                float3 nearby = float3(0.0);
                for (int row = 0; row < 3; row++)
                {
                    float radialWeight = (row == 1) ? 0.5 : 0.25;
                    float2 centre = float2(pixel) + ((normal * reach) * (0.1500000059604644775390625 + (0.3499999940395355224609375 * float(row))));
                    for (int column = 0; column < 3; column++)
                    {
                        float weight = radialWeight * ((column == 1) ? 0.5 : 0.25);
                        float2 param_86 = centre + ((tangent * spread) * float(column - 1));
                        bool param_87 = false;
                        nearby += (blur_input_color_at(param_86, param_87, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface).xyz * weight);
                    }
                }
                float energy = fast::max(nearby.x, fast::max(nearby.y, nearby.z));
                float3 rimColour = mix(float3(1.0), nearby / float3(fast::max(energy, 9.9999997473787516355514526367188e-05)), float3(smoothstep(0.0, 0.180000007152557373046875, energy) * 0.949999988079071044921875)) * mix(0.85000002384185791015625, 1.0, smoothstep(0.0, 0.64999997615814208984375, energy));
                if (paintedGlass)
                {
                    rimColour = mix(rimColour, fast::min((source.xyz * 1.12000000476837158203125) + float3(0.07999999821186065673828125), float3(1.0)), float3(0.819999992847442626953125));
                }
                float chroma = fast::max(source.x, fast::max(source.y, source.z)) - fast::min(source.x, fast::min(source.y, source.z));
                float _3741;
                if (paintedGlass)
                {
                    _3741 = smoothstep(0.64999997615814208984375, 0.87999999523162841796875, dot(source.xyz, float3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))) * (1.0 - smoothstep(0.07999999821186065673828125, 0.180000007152557373046875, chroma));
                }
                else
                {
                    _3741 = 0.0;
                }
                float lightFinish = _3741;
                float _3757;
                if (paintedGlass)
                {
                    _3757 = mix(1.0, 1.17999994754791259765625, lightFinish);
                }
                else
                {
                    _3757 = 2.2000000476837158203125;
                }
                float reflectionWhite = _3757;
                float4 _3768 = source;
                float4 _3776 = source;
                float3 _3778 = _3776.xyz + ((fast::max(float3(reflectionWhite) - _3768.xyz, float3(0.0)) * rimColour) * highlight);
                source.x = _3778.x;
                source.y = _3778.y;
                source.z = _3778.z;
            }
        }
    }
    else
    {
        uint param_88 = passIndex;
        bool param_89 = true;
        int2 param_90 = pixel;
        float2 param_91 = blur_axis(param_88, param_89);
        float param_92 = radius;
        bool param_93 = true;
        source = sample_axis_blur(param_90, param_91, param_92, param_93, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
    }
    float2 param_94 = p_1;
    float gradientAlpha = blur_gradient_alpha_factor(param_94, pc);
    if (pc.effect0.z > 0.5)
    {
        uiBlurSurface.write(source, uint2(pixel));
        return;
    }
    float4 blurred_2 = float4(source.xyz, fast::clamp(((source.w * pc.effect0.w) * mask) * gradientAlpha, 0.0, 1.0));
    if (blurred_2.w <= 0.001000000047497451305389404296875)
    {
        return;
    }
    spvImageFence(uiBlurSurface);
    float4 dst_2 = uiBlurSurface.read(uint2(pixel));
    float4 param_95 = dst_2;
    float4 param_96 = blurred_2;
    uiBlurSurface.write(blend_over(param_95, param_96), uint2(pixel));
}
