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

constant spvUnsafeArray<float, 41> _1703 = spvUnsafeArray<float, 41>({ -1.0, -0.949999988079071044921875, -0.89999997615814208984375, -0.85000002384185791015625, -0.800000011920928955078125, -0.75, -0.699999988079071044921875, -0.64999997615814208984375, -0.60000002384185791015625, -0.550000011920928955078125, -0.5, -0.449999988079071044921875, -0.4000000059604644775390625, -0.3499999940395355224609375, -0.300000011920928955078125, -0.25, -0.20000000298023223876953125, -0.1500000059604644775390625, -0.100000001490116119384765625, -0.0500000007450580596923828125, 0.0, 0.0500000007450580596923828125, 0.100000001490116119384765625, 0.1500000059604644775390625, 0.20000000298023223876953125, 0.25, 0.300000011920928955078125, 0.3499999940395355224609375, 0.4000000059604644775390625, 0.449999988079071044921875, 0.5, 0.550000011920928955078125, 0.60000002384185791015625, 0.64999997615814208984375, 0.699999988079071044921875, 0.75, 0.800000011920928955078125, 0.85000002384185791015625, 0.89999997615814208984375, 0.949999988079071044921875, 1.0 });
constant spvUnsafeArray<float, 21> _1712 = spvUnsafeArray<float, 21>({ -1.0, -0.89999997615814208984375, -0.800000011920928955078125, -0.699999988079071044921875, -0.60000002384185791015625, -0.5, -0.4000000059604644775390625, -0.300000011920928955078125, -0.20000000298023223876953125, -0.100000001490116119384765625, 0.0, 0.100000001490116119384765625, 0.20000000298023223876953125, 0.300000011920928955078125, 0.4000000059604644775390625, 0.5, 0.60000002384185791015625, 0.699999988079071044921875, 0.800000011920928955078125, 0.89999997615814208984375, 1.0 });
constant spvUnsafeArray<float, 41> _1744 = spvUnsafeArray<float, 41>({ 0.017999999225139617919921875, 0.02700000070035457611083984375, 0.0390000008046627044677734375, 0.056000001728534698486328125, 0.076999999582767486572265625, 0.104999996721744537353515625, 0.1410000026226043701171875, 0.185000002384185791015625, 0.23700000345706939697265625, 0.2980000078678131103515625, 0.3680000007152557373046875, 0.444999992847442626953125, 0.527000010013580322265625, 0.612999975681304931640625, 0.698000013828277587890625, 0.778999984264373779296875, 0.8519999980926513671875, 0.913999974727630615234375, 0.961000025272369384765625, 0.9900000095367431640625, 1.0, 0.9900000095367431640625, 0.961000025272369384765625, 0.913999974727630615234375, 0.8519999980926513671875, 0.778999984264373779296875, 0.698000013828277587890625, 0.612999975681304931640625, 0.527000010013580322265625, 0.444999992847442626953125, 0.3680000007152557373046875, 0.2980000078678131103515625, 0.23700000345706939697265625, 0.185000002384185791015625, 0.1410000026226043701171875, 0.104999996721744537353515625, 0.076999999582767486572265625, 0.056000001728534698486328125, 0.0390000008046627044677734375, 0.02700000070035457611083984375, 0.017999999225139617919921875 });
constant spvUnsafeArray<float, 21> _1758 = spvUnsafeArray<float, 21>({ 0.0199999995529651641845703125, 0.0390000008046627044677734375, 0.07400000095367431640625, 0.13500000536441802978515625, 0.236000001430511474609375, 0.3860000073909759521484375, 0.60699999332427978515625, 0.800000011920928955078125, 0.94599997997283935546875, 0.986000001430511474609375, 1.0, 0.986000001430511474609375, 0.94599997997283935546875, 0.800000011920928955078125, 0.60699999332427978515625, 0.3860000073909759521484375, 0.236000001430511474609375, 0.13500000536441802978515625, 0.07400000095367431640625, 0.0390000008046627044677734375, 0.0199999995529651641845703125 });

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
    bool _617 = outside.x == 0.0;
    bool _624;
    if (!_617)
    {
        _624 = outside.y == 0.0;
    }
    else
    {
        _624 = _617;
    }
    float _625;
    if (_624)
    {
        _625 = outside.x + outside.y;
    }
    else
    {
        _625 = powr(powr(outside.x, cornerPower) + powr(outside.y, cornerPower), 1.0 / cornerPower);
    }
    float squircleNorm = _625;
    return (mix(roundedNorm, squircleNorm, fast::clamp(amount, 0.0, 1.0)) + fast::min(fast::max(q.x, q.y), 0.0)) - safeRadius;
}

static inline __attribute__((always_inline))
float notch_flare_size(thread const float2& size, thread const float& amount, thread const float& depth)
{
    float progress = fast::clamp((fast::clamp(amount, 0.0, 1.0) - 0.85000002384185791015625) / 0.1500000059604644775390625, 0.0, 1.0);
    float _961;
    if (depth > 0.5)
    {
        _961 = depth;
    }
    else
    {
        _961 = fast::clamp(size.y * 0.62000000476837158203125, 0.0, 92.0);
    }
    float depthTarget = _961;
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
        float _544;
        if (p.x < 0.0)
        {
            _544 = radii.x;
        }
        else
        {
            _544 = radii.y;
        }
        return _544;
    }
    float _557;
    if (p.x < 0.0)
    {
        _557 = radii.w;
    }
    else
    {
        _557 = radii.z;
    }
    return _557;
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
    bool _714 = outside.x == 0.0;
    bool _721;
    if (!_714)
    {
        _721 = outside.y == 0.0;
    }
    else
    {
        _721 = _714;
    }
    float _722;
    if (_721)
    {
        _722 = outside.x + outside.y;
    }
    else
    {
        _722 = powr(powr(outside.x, cornerPower) + powr(outside.y, cornerPower), 1.0 / cornerPower);
    }
    float squircleNorm = _722;
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
    bool _1209 = (pc.data.y & 2097152u) != 0u;
    bool _1216;
    if (_1209)
    {
        _1216 = (pc.data.y & 4194304u) == 0u;
    }
    else
    {
        _1216 = _1209;
    }
    if (_1216)
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
    float _1280;
    if ((pc.data.x & 255u) == 1u)
    {
        _1280 = 0.0;
    }
    else
    {
        float _1288;
        if ((pc.data.y & 4194304u) != 0u)
        {
            _1288 = pc.effect0.y;
        }
        else
        {
            _1288 = pc.effect1.x;
        }
        _1280 = fast::clamp(_1288, 0.0, fast::min(halfSize_1.x, halfSize_1.y));
    }
    float radius = _1280;
    float squircleAmount = fast::clamp(pc.uvRect.x, 0.0, 1.0);
    float _1313;
    if (pc.uvRect.y <= 0.001000000047497451305389404296875)
    {
        _1313 = 4.0;
    }
    else
    {
        _1313 = pc.uvRect.y;
    }
    float squirclePower = fast::clamp(_1313, 2.0, 5.0);
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
    float _1393;
    if ((pc.data.y & 6291456u) != 0u)
    {
        _1393 = 0.0;
    }
    else
    {
        _1393 = fast::max(pc.effect1.z, 0.0);
    }
    float featherRadius = _1393;
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
    return smoothstep(1.0, -1.0, sdf);
}

static inline __attribute__((always_inline))
float gradient_t(thread const float2& localUv, constant Renderer2DConstants& pc)
{
    float2 _176;
    if ((pc.data.y & 2097152u) != 0u)
    {
        _176 = pc.uvRect.xy;
    }
    else
    {
        _176 = pc.color2.xy;
    }
    float2 start = _176;
    float2 _195;
    if ((pc.data.y & 2097152u) != 0u)
    {
        _195 = pc.uvRect.zw;
    }
    else
    {
        _195 = pc.color2.zw;
    }
    float2 end = _195;
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
    uint _1452;
    if ((pc.data.y & 4194304u) != 0u)
    {
        _1452 = 0u;
    }
    else
    {
        _1452 = pc.data.z & 65535u;
    }
    return _1452;
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
    float2 _1487;
    if (perpendicular)
    {
        _1487 = float2(-axis.y, axis.x);
    }
    else
    {
        _1487 = axis;
    }
    return _1487;
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
    bool _389 = (pc.data.y & 4194304u) != 0u;
    bool _397;
    if (_389)
    {
        _397 = (pc.data.z & 536870912u) != 0u;
    }
    else
    {
        _397 = _389;
    }
    if (_397)
    {
        float2 p = float2(pixel) + float2(0.5);
        bool _412 = all(p >= pc.color0.xy);
        bool _425;
        if (_412)
        {
            _425 = all(p <= (pc.color0.xy + pc.color0.zw));
        }
        else
        {
            _425 = _412;
        }
        if (_425)
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
    float4 _1502;
    if (readScratch)
    {
        spvImageFence(tempSurface);
        _1502 = tempSurface.read(uint2(pixel));
    }
    else
    {
        int2 param = pixel;
        _1502 = source_color(param, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface);
    }
    return _1502;
}

static inline __attribute__((always_inline))
float4 blur_input_color_at(thread const float2& coord, thread const bool& readScratch, constant Renderer2DConstants& pc, texture2d<float, access::read_write> sceneColorBuffer, texture2d<float> uiStaticSurface, texture2d<float> uiExternalBackdropSurface, texture2d<float, access::read_write> tempSurface, texture2d<float, access::read_write> uiBlurSurface)
{
    int2 screenSize = int2(uiBlurSurface.get_width(), uiBlurSurface.get_height());
    float2 clamped = fast::clamp(coord, float2(0.0), float2(screenSize - int2(1)));
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
    float _1666;
    float _1721;
    for (int i = 0; i < activeSampleCount; i++)
    {
        if (highQualityForeground)
        {
            _1666 = _1703[i];
        }
        else
        {
            _1666 = _1712[i];
        }
        float offset = _1666;
        if (highQualityForeground)
        {
            _1721 = _1744[i];
        }
        else
        {
            _1721 = _1758[i];
        }
        float sampleWeight = _1721;
        float2 sampleCoord = pixelCoord + ((axis * offset) * radius);
        float2 param_2 = sampleCoord;
        bool param_3 = readScratch;
        float4 sampleColor = blur_input_color_at(param_2, param_3, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
        weightedRgb += ((sampleColor.xyz * sampleColor.w) * sampleWeight);
        alphaSum += (sampleColor.w * sampleWeight);
        weightSum += sampleWeight;
    }
    float _1801;
    if (weightSum > 9.9999997473787516355514526367188e-05)
    {
        _1801 = alphaSum / weightSum;
    }
    else
    {
        _1801 = 0.0;
    }
    float sourceAlpha = _1801;
    float3 _1812;
    if (alphaSum > 9.9999997473787516355514526367188e-05)
    {
        _1812 = weightedRgb / float3(alphaSum);
    }
    else
    {
        _1812 = float3(0.0);
    }
    float3 sourceRgb = _1812;
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
    bool _1882 = pixel.x < 0;
    bool _1889;
    if (!_1882)
    {
        _1889 = pixel.y < 0;
    }
    else
    {
        _1889 = _1882;
    }
    bool _1898;
    if (!_1889)
    {
        _1898 = pixel.x >= screenSize.x;
    }
    else
    {
        _1898 = _1889;
    }
    bool _1907;
    if (!_1898)
    {
        _1907 = pixel.y >= screenSize.y;
    }
    else
    {
        _1907 = _1898;
    }
    if (_1907)
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
            float alpha = mix(dst.w, src.w, coverage);
            float3 premultiplied = mix(dst.xyz * dst.w, src.xyz * src.w, float3(coverage));
            float3 _1992;
            if (alpha > 9.9999997473787516355514526367188e-05)
            {
                _1992 = premultiplied / float3(alpha);
            }
            else
            {
                _1992 = float3(0.0);
            }
            sceneColorBuffer.write(float4(_1992, alpha), uint2(pixel));
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
    float _2107;
    if ((pc.data.y & 4194304u) != 0u)
    {
        _2107 = 0.0;
    }
    else
    {
        _2107 = fast::max(pc.effect1.z, 0.0);
    }
    float featherRadius = _2107;
    float boundsPad = featherRadius + primitive_bounds_padding(pc);
    bool _2125 = p_1.x < (pc.rect.x - boundsPad);
    bool _2136;
    if (!_2125)
    {
        _2136 = p_1.y < (pc.rect.y - boundsPad);
    }
    else
    {
        _2136 = _2125;
    }
    bool _2150;
    if (!_2136)
    {
        _2150 = p_1.x > ((pc.rect.x + pc.rect.z) + boundsPad);
    }
    else
    {
        _2150 = _2136;
    }
    bool _2164;
    if (!_2150)
    {
        _2164 = p_1.y > ((pc.rect.y + pc.rect.w) + boundsPad);
    }
    else
    {
        _2164 = _2150;
    }
    if (_2164)
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
        float edge = fast::clamp(shortSide * 0.1599999964237213134765625, 4.0, 44.0);
        float band = fast::clamp(1.0 + (_distance / edge), 0.0, 1.0);
        float2 normal = float2(0.0);
        if ((band > 0.0) || (_distance > (-16.0)))
        {
            float2 param_20 = p_1 + float2(0.5, 0.0);
            float2 param_21 = p_1 - float2(0.5, 0.0);
            float2 param_22 = p_1 + float2(0.0, 0.5);
            float2 param_23 = p_1 - float2(0.0, 0.5);
            normal = float2(shape_sdf(param_20, pc) - shape_sdf(param_21, pc), shape_sdf(param_22, pc) - shape_sdf(param_23, pc));
            normal /= float2(fast::max(length(normal), 9.9999997473787516355514526367188e-05));
        }
        float strength = float(pc.data.z & 127u) / 127.0;
        float bend = fast::min((strength * 56.0) * fast::clamp(pc.rect.z / 500.0, 0.3499999940395355224609375, 2.0), shortSide * 0.37999999523162841796875);
        float2 samplePoint = float2(pixel) - (((normal * bend) * band) * band);
        float2 param_24 = samplePoint;
        float2 param_25 = float2(0.0, 1.0);
        float param_26 = radius;
        bool param_27 = true;
        source = sample_axis_blur_at(param_24, param_25, param_26, param_27, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
        float dispersion = ((float((pc.data.z >> 7u) & 127u) / 127.0) * 3.0) * band;
        if (dispersion > 0.0)
        {
            float2 param_28 = samplePoint + (normal * dispersion);
            float2 param_29 = float2(0.0, 1.0);
            float param_30 = radius;
            bool param_31 = true;
            source.x = sample_axis_blur_at(param_28, param_29, param_30, param_31, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface).x;
            float2 param_32 = samplePoint - (normal * dispersion);
            float2 param_33 = float2(0.0, 1.0);
            float param_34 = radius;
            bool param_35 = true;
            source.z = sample_axis_blur_at(param_32, param_33, param_34, param_35, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface).z;
        }
        if (_distance > (-5.0))
        {
            float phase = (float((pc.data.z >> 21u) & 255u) / 255.0) * 6.283185482025146484375;
            float _2348;
            if ((pc.data.y & 8192u) != 0u)
            {
                _2348 = 1.0;
            }
            else
            {
                _2348 = 0.699999988079071044921875 + (0.300000011920928955078125 * cos((phase + (((p_1.x - pc.rect.x) / fast::max(pc.rect.z, 1.0)) * 3.1415927410125732421875)) + (normal.y * 0.800000011920928955078125)));
            }
            float light = _2348;
            float rim = (float((pc.data.z >> 14u) & 127u) / 127.0) * 0.550000011920928955078125;
            float vertical = normal.y * normal.y;
            float core = exp(((-(_distance + 0.25)) * (_distance + 0.25)) / 0.60000002384185791015625);
            float soft = exp(((-(_distance + 1.2999999523162841796875)) * (_distance + 1.2999999523162841796875)) / 6.0);
            float highlight = ((rim * ((0.519999980926513671875 * core) + (0.180000007152557373046875 * soft))) * (0.07999999821186065673828125 + ((0.920000016689300537109375 * vertical) * vertical))) * light;
            float4 _2437 = source;
            float3 _2439 = _2437.xyz * (1.0 - ((rim * 0.2800000011920928955078125) * exp(((-_distance) * _distance) / 0.3499999940395355224609375)));
            source.x = _2439.x;
            source.y = _2439.y;
            source.z = _2439.z;
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
                        float2 param_36 = centre + ((tangent * spread) * float(column - 1));
                        bool param_37 = false;
                        nearby += (blur_input_color_at(param_36, param_37, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface).xyz * weight);
                    }
                }
                float energy = fast::max(nearby.x, fast::max(nearby.y, nearby.z));
                float3 reflectedColour = nearby / float3(fast::max(energy, 9.9999997473787516355514526367188e-05));
                float3 rimColour = mix(float3(1.0), reflectedColour, float3(fast::clamp(energy * 10.0, 0.0, 0.949999988079071044921875)));
                float4 _2555 = source;
                float3 _2557 = _2555.xyz + (rimColour * highlight);
                source.x = _2557.x;
                source.y = _2557.y;
                source.z = _2557.z;
            }
        }
    }
    else
    {
        uint param_38 = passIndex;
        bool param_39 = true;
        int2 param_40 = pixel;
        float2 param_41 = blur_axis(param_38, param_39);
        float param_42 = radius;
        bool param_43 = true;
        source = sample_axis_blur(param_40, param_41, param_42, param_43, pc, sceneColorBuffer, uiStaticSurface, uiExternalBackdropSurface, tempSurface, uiBlurSurface);
    }
    float2 param_44 = p_1;
    float gradientAlpha = blur_gradient_alpha_factor(param_44, pc);
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
    float4 param_45 = dst_2;
    float4 param_46 = blurred_2;
    uiBlurSurface.write(blend_over(param_45, param_46), uint2(pixel));
}
