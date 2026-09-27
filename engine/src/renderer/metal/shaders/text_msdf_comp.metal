#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

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
int2 dispatch_extent(constant Renderer2DConstants& pc)
{
    return int2(int(pc.data.x & 65535u), int((pc.data.x >> 16u) & 65535u));
}

static inline __attribute__((always_inline))
int2 dispatch_origin(constant Renderer2DConstants& pc)
{
    return int2(int(pc.data.w & 65535u), int((pc.data.w >> 16u) & 65535u));
}

static inline __attribute__((always_inline))
float glyph_weight_expansion_pixels(constant Renderer2DConstants& pc)
{
    uint _packed = (pc.data.z >> 2u) & 4095u;
    return float(int(_packed) - 2048) / 256.0;
}

static inline __attribute__((always_inline))
int2 font_atlas_size(texture2d<float> fontAtlas)
{
    int2 packedSize = int2(fontAtlas.get_width(), fontAtlas.get_height());
    return int2(packedSize.x * 4, packedSize.y);
}

static inline __attribute__((always_inline))
float load_font_atlas(thread const int2& pixel, texture2d<float> fontAtlas)
{
    float4 _packed = fontAtlas.read(uint2(int2(pixel.x / 4, pixel.y)));
    return _packed[pixel.x & 3];
}

static inline __attribute__((always_inline))
float sample_font_atlas(thread const float2& atlasUv, texture2d<float> fontAtlas)
{
    int2 atlasSize = font_atlas_size(fontAtlas);
    float2 texel = (fast::clamp(atlasUv, float2(0.0), float2(1.0)) * float2(atlasSize)) - float2(0.5);
    int2 basePixel = int2(floor(texel));
    float2 blend = fract(texel);
    int2 maximumPixel = atlasSize - int2(1);
    int2 p00 = clamp(basePixel, int2(0), maximumPixel);
    int2 p10 = clamp(basePixel + int2(1, 0), int2(0), maximumPixel);
    int2 p01 = clamp(basePixel + int2(0, 1), int2(0), maximumPixel);
    int2 p11 = clamp(basePixel + int2(1), int2(0), maximumPixel);
    int2 param = p00;
    int2 param_1 = p10;
    float top = mix(load_font_atlas(param, fontAtlas), load_font_atlas(param_1, fontAtlas), blend.x);
    int2 param_2 = p01;
    int2 param_3 = p11;
    float bottom = mix(load_font_atlas(param_2, fontAtlas), load_font_atlas(param_3, fontAtlas), blend.x);
    return mix(top, bottom, blend.y);
}

static inline __attribute__((always_inline))
float atlas_pixels_per_screen_pixel(constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    int2 atlasSize = font_atlas_size(fontAtlas);
    float2 glyphAtlasSize = fast::max((pc.uvRect.zw - pc.uvRect.xy) * float2(atlasSize), float2(1.0));
    float2 atlasPerScreen = glyphAtlasSize / fast::max(pc.rect.zw, float2(1.0));
    return fast::max(fast::min(atlasPerScreen.x, atlasPerScreen.y), 0.001000000047497451305389404296875);
}

static inline __attribute__((always_inline))
float atlas_signed_distance_pixels(thread const float2& localUv, constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    float2 clampedUv = fast::clamp(localUv, float2(0.0), float2(1.0));
    float2 atlasUv = mix(pc.uvRect.xy, pc.uvRect.zw, clampedUv);
    float range = fast::max(pc.effect0.x, 1.0);
    float2 param = atlasUv;
    float atlasDistance = ((sample_font_atlas(param, fontAtlas) - 0.705882370471954345703125) * range) / 0.705882370471954345703125;
    float2 outsidePixels = (localUv - clampedUv) * pc.rect.zw;
    return (atlasDistance / atlas_pixels_per_screen_pixel(pc, fontAtlas)) - length(outsidePixels);
}

static inline __attribute__((always_inline))
float atlas_sdf_alpha(thread const float2& localUv, thread const float& expansionPixels, constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    float2 param = localUv;
    float _distance = atlas_signed_distance_pixels(param, pc, fontAtlas) + expansionPixels;
    float edgeSoftness = 0.7200000286102294921875;
    return smoothstep(-edgeSoftness, edgeSoftness, _distance);
}

static inline __attribute__((always_inline))
float pseudo_glyph_sdf(thread const float2& uv, constant Renderer2DConstants& pc)
{
    float glyphCount = fast::max(1.0, floor(pc.rect.z / fast::max(pc.rect.w * 0.519999980926513671875, 1.0)));
    float2 cell = float2(fract(uv.x * glyphCount), uv.y);
    float stem = abs(cell.x - 0.2800000011920928955078125) - 0.07500000298023223876953125;
    float2 bowlPos = (cell - float2(0.519999980926513671875)) * float2(1.14999997615814208984375, 0.89999997615814208984375);
    float bowl = abs(length(bowlPos) - 0.2800000011920928955078125) - 0.054999999701976776123046875;
    float cap = abs(cell.y - 0.23999999463558197021484375) - 0.054999999701976776123046875;
    float sdf = fast::min(fast::min(stem, bowl), cap);
    return sdf * pc.rect.w;
}

static inline __attribute__((always_inline))
float median(thread const float& r, thread const float& g, thread const float& b)
{
    return fast::max(fast::min(r, g), fast::min(fast::max(r, g), b));
}

static inline __attribute__((always_inline))
float msdf_alpha(thread const float2& uv, thread const float& offset, constant Renderer2DConstants& pc)
{
    float2 param = uv;
    float sdf = pseudo_glyph_sdf(param, pc) - offset;
    float pxRange = fast::max(pc.effect0.x, 1.0);
    float3 msdf = float3(0.5 - (sdf / pxRange));
    float param_1 = msdf.x;
    float param_2 = msdf.y;
    float param_3 = msdf.z;
    float signedDistance = median(param_1, param_2, param_3) - 0.5;
    return fast::clamp((signedDistance * pxRange) + 0.5, 0.0, 1.0);
}

static inline __attribute__((always_inline))
float glyph_alpha(thread const float2& localUv, thread const bool& atlasText, thread const float& expansionPixels, constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    if (atlasText)
    {
        float2 param = localUv;
        float param_1 = expansionPixels;
        return atlas_sdf_alpha(param, param_1, pc, fontAtlas);
    }
    bool _1319 = localUv.x < 0.0;
    bool _1326;
    if (!_1319)
    {
        _1326 = localUv.y < 0.0;
    }
    else
    {
        _1326 = _1319;
    }
    bool _1333;
    if (!_1326)
    {
        _1333 = localUv.x > 1.0;
    }
    else
    {
        _1333 = _1326;
    }
    bool _1340;
    if (!_1333)
    {
        _1340 = localUv.y > 1.0;
    }
    else
    {
        _1340 = _1333;
    }
    if (_1340)
    {
        return 0.0;
    }
    float2 param_2 = localUv;
    float param_3 = -expansionPixels;
    return msdf_alpha(param_2, param_3, pc);
}

static inline __attribute__((always_inline))
float pseudo_signed_distance_pixels(thread const float2& localUv, constant Renderer2DConstants& pc)
{
    float2 clampedUv = fast::clamp(localUv, float2(0.0), float2(1.0));
    float2 outsidePixels = (localUv - clampedUv) * pc.rect.zw;
    float2 param = clampedUv;
    return (-pseudo_glyph_sdf(param, pc)) - length(outsidePixels);
}

static inline __attribute__((always_inline))
float glyph_signed_distance_pixels(thread const float2& localUv, thread const bool& atlasText, constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    float _593;
    if (atlasText)
    {
        float2 param = localUv;
        _593 = atlas_signed_distance_pixels(param, pc, fontAtlas);
    }
    else
    {
        float2 param_1 = localUv;
        _593 = pseudo_signed_distance_pixels(param_1, pc);
    }
    return _593;
}

static inline __attribute__((always_inline))
float atlas_boundary_fade(thread const float2& localUv, thread const bool& atlasText, thread const float& radiusPixels, constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    if (!atlasText)
    {
        return 1.0;
    }
    if (radiusPixels > 0.5)
    {
        float2 outsideUv = fast::max(fast::max(-localUv, localUv - float2(1.0)), float2(0.0));
        float outsidePixels = length(outsideUv * pc.rect.zw);
        return 1.0 - smoothstep(fast::max(radiusPixels * 0.7799999713897705078125, 1.0), radiusPixels + 1.0, outsidePixels);
    }
    float2 edgePixels = fast::min(localUv, float2(1.0) - localUv) * pc.rect.zw;
    float edgeDistance = fast::min(edgePixels.x, edgePixels.y);
    float rangeScreen = fast::max(pc.effect0.x / atlas_pixels_per_screen_pixel(pc, fontAtlas), 1.0);
    float fadeWidth = fast::clamp(fast::min(fast::max(radiusPixels, 1.0) * 0.3499999940395355224609375, rangeScreen * 0.2199999988079071044921875), 1.0, 10.0);
    return smoothstep(0.0, fadeWidth, edgeDistance);
}

static inline __attribute__((always_inline))
float blurred_glyph_alpha(thread const float2& localUv, thread const bool& atlasText, thread const float& radiusPixels, thread const float& expansionPixels, constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    float radius = fast::max(radiusPixels, 0.0);
    if (radius <= 0.5)
    {
        float2 param = localUv;
        bool param_1 = atlasText;
        float param_2 = expansionPixels;
        return glyph_alpha(param, param_1, param_2, pc, fontAtlas);
    }
    float2 param_3 = localUv;
    bool param_4 = atlasText;
    float _distance = glyph_signed_distance_pixels(param_3, param_4, pc, fontAtlas) + expansionPixels;
    float outsideDistance = fast::max(-_distance, 0.0);
    float sigma = fast::max(radius * 0.4600000083446502685546875, 0.75);
    float gaussian = exp((-(outsideDistance * outsideDistance)) / ((2.0 * sigma) * sigma));
    float cutoff = 1.0 - smoothstep(fast::max(radius * 0.449999988079071044921875, 0.0), radius + 1.0, outsideDistance);
    float insideCoverage = smoothstep(-0.7200000286102294921875, 0.7200000286102294921875, _distance);
    float coverage = fast::max(insideCoverage, gaussian * cutoff);
    float2 param_5 = localUv;
    bool param_6 = atlasText;
    float param_7 = radius;
    return fast::clamp(coverage * atlas_boundary_fade(param_5, param_6, param_7, pc, fontAtlas), 0.0, 1.0);
}

static inline __attribute__((always_inline))
float shadow_alpha(thread const float2& localUv, thread const bool& atlasText, constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    float2 param = localUv;
    bool param_1 = atlasText;
    float param_2 = pc.effect1.z;
    float param_3 = glyph_weight_expansion_pixels(pc);
    return blurred_glyph_alpha(param, param_1, param_2, param_3, pc, fontAtlas);
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
float4 fill_color(thread const float2& uv, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 1u) == 0u)
    {
        return pc.color0;
    }
    if ((pc.data.y & 256u) != 0u)
    {
        float t = fast::clamp((uv.x * 0.800000011920928955078125) + (uv.y * 0.20000000298023223876953125), 0.0, 1.0);
        return mix(pc.color0, pc.color1, float4(t));
    }
    float2 a = pc.uvRect.xy;
    float2 b = pc.uvRect.zw;
    float2 axis = b - a;
    float denom = fast::max(dot(axis, axis), 9.9999997473787516355514526367188e-05);
    float t_1 = fast::clamp(dot(uv - a, axis) / denom, 0.0, 1.0);
    return mix(pc.color0, pc.color1, float4(t_1));
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
    float _910;
    if (depth > 0.5)
    {
        _910 = depth;
    }
    else
    {
        _910 = fast::clamp(size.y * 0.62000000476837158203125, 0.0, 92.0);
    }
    float depthTarget = _910;
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
        float _813;
        if (p.x < 0.0)
        {
            _813 = radii.x;
        }
        else
        {
            _813 = radii.y;
        }
        return _813;
    }
    float _826;
    if (p.x < 0.0)
    {
        _826 = radii.w;
    }
    else
    {
        _826 = radii.z;
    }
    return _826;
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
float sd_rounded_box(thread const float2& p, thread const float2& halfSize, thread const float& radius)
{
    float2 q = abs(p) - fast::max(halfSize - float2(radius), float2(0.0));
    return (length(fast::max(q, float2(0.0))) + fast::min(fast::max(q.x, q.y), 0.0)) - radius;
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
float text_edge_fade_alpha(thread const float2& samplePoint, constant Renderer2DConstants& pc)
{
    if ((pc.data.y & 33554432u) == 0u)
    {
        return 1.0;
    }
    uint mode = (pc.data.z >> 30u) & 3u;
    float fadeStart = float(int((pc.data.z >> 14u) & 255u) - 128) / 4.0;
    float fadeEnd = float(int((pc.data.z >> 22u) & 255u) - 128) / 4.0;
    float localX = samplePoint.x - pc.rect.x;
    float t = fast::clamp((localX - fadeStart) / fast::max(fadeEnd - fadeStart, 0.001000000047497451305389404296875), 0.0, 1.0);
    t = ((t * t) * t) * ((t * ((t * 6.0) - 15.0)) + 10.0);
    if (mode == 1u)
    {
        return t;
    }
    if (mode == 2u)
    {
        return 1.0 - t;
    }
    return 1.0;
}

static inline __attribute__((always_inline))
float4 shade_text_sample(thread const float2& samplePoint, thread const bool& atlasText, thread const bool& renderUnderlay, thread const bool& renderForeground, thread const bool& renderGlow, constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    float2 uv = (samplePoint - pc.rect.xy) / fast::max(pc.rect.zw, float2(1.0));
    float fillAlpha = 0.0;
    float outlineAlpha = 0.0;
    float weightExpansion = glyph_weight_expansion_pixels(pc);
    if (renderForeground || renderGlow)
    {
        float _1601;
        if (atlasText)
        {
            float2 param = uv;
            float param_1 = weightExpansion;
            _1601 = atlas_sdf_alpha(param, param_1, pc, fontAtlas);
        }
        else
        {
            float2 param_2 = fast::clamp(uv, float2(0.0), float2(1.0));
            float param_3 = -weightExpansion;
            _1601 = msdf_alpha(param_2, param_3, pc);
        }
        fillAlpha = _1601;
        if ((pc.data.y & 2u) != 0u)
        {
            float outlineExpansion = weightExpansion + pc.effect0.y;
            float _1631;
            if (atlasText)
            {
                float2 param_4 = uv;
                float param_5 = outlineExpansion;
                _1631 = atlas_sdf_alpha(param_4, param_5, pc, fontAtlas);
            }
            else
            {
                float2 param_6 = fast::clamp(uv, float2(0.0), float2(1.0));
                float param_7 = -outlineExpansion;
                _1631 = msdf_alpha(param_6, param_7, pc);
            }
            float outlineCoverage = _1631;
            outlineAlpha = fast::max(outlineCoverage - fillAlpha, 0.0);
        }
    }
    float4 src = float4(0.0);
    bool _1661;
    if (renderUnderlay)
    {
        _1661 = (pc.data.y & 8u) != 0u;
    }
    else
    {
        _1661 = renderUnderlay;
    }
    if (_1661)
    {
        float2 shadowUv = ((samplePoint - pc.effect1.xy) - pc.rect.xy) / fast::max(pc.rect.zw, float2(1.0));
        float2 param_8 = shadowUv;
        bool param_9 = atlasText;
        float shadowCoverage = shadow_alpha(param_8, param_9, pc, fontAtlas);
        float shadowAlpha = ((shadowCoverage * pc.color2.w) * pc.effect0.w) * 0.519999980926513671875;
        float4 param_10 = src;
        float4 param_11 = float4(pc.color2.xyz, shadowAlpha);
        src = blend_over(param_10, param_11);
    }
    if (renderGlow)
    {
        float2 param_12 = uv;
        bool param_13 = atlasText;
        float param_14 = pc.effect0.z;
        float param_15 = weightExpansion;
        float glowCoverage = blurred_glyph_alpha(param_12, param_13, param_14, param_15, pc, fontAtlas);
        float glowAlpha = fast::max((glowCoverage - fillAlpha) - outlineAlpha, 0.0) * 0.85000002384185791015625;
        if (glowAlpha > 0.001000000047497451305389404296875)
        {
            float4 param_16 = src;
            float4 param_17 = float4(pc.color2.xyz, (pc.color2.w * glowAlpha) * pc.effect0.w);
            src = blend_over(param_16, param_17);
        }
    }
    if (renderForeground)
    {
        float2 fillUv = fast::clamp(uv, float2(0.0), float2(1.0));
        if (outlineAlpha > 0.001000000047497451305389404296875)
        {
            float4 param_18 = src;
            float4 param_19 = float4(pc.color2.xyz, (pc.color2.w * outlineAlpha) * pc.effect0.w);
            src = blend_over(param_18, param_19);
        }
        float2 param_20 = fillUv;
        float4 fillLayer = fill_color(param_20, pc);
        fillLayer.w *= (fillAlpha * pc.effect0.w);
        float4 param_21 = src;
        float4 param_22 = fillLayer;
        src = blend_over(param_21, param_22);
    }
    float2 param_23 = samplePoint;
    float2 param_24 = samplePoint;
    src.w *= (inherited_shape_mask_alpha(param_23, pc) * text_edge_fade_alpha(param_24, pc));
    return (src.w > 0.001000000047497451305389404296875) ? src : float4(0.0);
}

static inline __attribute__((always_inline))
float4 resolve_text(thread const float2& pixelCenter, thread const bool& atlasText, thread const bool& renderUnderlay, thread const bool& renderForeground, thread const bool& renderGlow, constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    float3 premultiplied = float3(0.0);
    float alpha = 0.0;
    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            float2 offset = ((float2(float(x), float(y)) + float2(0.5)) * 0.25) - float2(0.5);
            float2 param = pixelCenter + offset;
            bool param_1 = atlasText;
            bool param_2 = renderUnderlay;
            bool param_3 = renderForeground;
            bool param_4 = renderGlow;
            float4 sampleColor = shade_text_sample(param, param_1, param_2, param_3, param_4, pc, fontAtlas);
            premultiplied += (sampleColor.xyz * sampleColor.w);
            alpha += sampleColor.w;
        }
    }
    premultiplied *= 0.0625;
    alpha *= 0.0625;
    float3 _1879;
    if (alpha > 9.9999997473787516355514526367188e-05)
    {
        _1879 = premultiplied / float3(alpha);
    }
    else
    {
        _1879 = float3(0.0);
    }
    float3 rgb = _1879;
    return float4(rgb, alpha);
}

static inline __attribute__((always_inline))
void accumulate_text_sample(thread const float2& samplePoint, thread const float& weight, thread const bool& atlasText, thread const bool& renderUnderlay, thread const bool& renderForeground, thread const bool& renderGlow, thread float3& premultiplied, thread float& alpha, thread float& weightSum, constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    float2 param = samplePoint;
    bool param_1 = atlasText;
    bool param_2 = renderUnderlay;
    bool param_3 = renderForeground;
    bool param_4 = renderGlow;
    float4 sampleColor = shade_text_sample(param, param_1, param_2, param_3, param_4, pc, fontAtlas);
    premultiplied += ((sampleColor.xyz * sampleColor.w) * weight);
    alpha += (sampleColor.w * weight);
    weightSum += weight;
}

static inline __attribute__((always_inline))
float4 resolve_blurred_text(thread const float2& pixelCenter, thread const bool& atlasText, thread const bool& renderUnderlay, thread const bool& renderForeground, thread const bool& renderGlow, constant Renderer2DConstants& pc, texture2d<float> fontAtlas)
{
    float radius = fast::clamp(pc.effect1.w, 0.0, 40.0);
    if (((pc.data.y & 32u) == 0u) || (radius <= 0.5))
    {
        float2 param = pixelCenter;
        bool param_1 = atlasText;
        bool param_2 = renderUnderlay;
        bool param_3 = renderForeground;
        bool param_4 = renderGlow;
        return resolve_text(param, param_1, param_2, param_3, param_4, pc, fontAtlas);
    }
    float3 premultiplied = float3(0.0);
    float alpha = 0.0;
    float weightSum = 0.0;
    float nearRadius = radius * 0.319999992847442626953125;
    float midRadius = radius * 0.579999983310699462890625;
    float farRadius = radius * 0.87999999523162841796875;
    float2 param_5 = pixelCenter;
    float param_6 = 0.180000007152557373046875;
    bool param_7 = atlasText;
    bool param_8 = renderUnderlay;
    bool param_9 = renderForeground;
    bool param_10 = renderGlow;
    float3 param_11 = premultiplied;
    float param_12 = alpha;
    float param_13 = weightSum;
    accumulate_text_sample(param_5, param_6, param_7, param_8, param_9, param_10, param_11, param_12, param_13, pc, fontAtlas);
    premultiplied = param_11;
    alpha = param_12;
    weightSum = param_13;
    float2 param_14 = pixelCenter + float2(nearRadius, 0.0);
    float param_15 = 0.100000001490116119384765625;
    bool param_16 = atlasText;
    bool param_17 = renderUnderlay;
    bool param_18 = renderForeground;
    bool param_19 = renderGlow;
    float3 param_20 = premultiplied;
    float param_21 = alpha;
    float param_22 = weightSum;
    accumulate_text_sample(param_14, param_15, param_16, param_17, param_18, param_19, param_20, param_21, param_22, pc, fontAtlas);
    premultiplied = param_20;
    alpha = param_21;
    weightSum = param_22;
    float2 param_23 = pixelCenter + float2(-nearRadius, 0.0);
    float param_24 = 0.100000001490116119384765625;
    bool param_25 = atlasText;
    bool param_26 = renderUnderlay;
    bool param_27 = renderForeground;
    bool param_28 = renderGlow;
    float3 param_29 = premultiplied;
    float param_30 = alpha;
    float param_31 = weightSum;
    accumulate_text_sample(param_23, param_24, param_25, param_26, param_27, param_28, param_29, param_30, param_31, pc, fontAtlas);
    premultiplied = param_29;
    alpha = param_30;
    weightSum = param_31;
    float2 param_32 = pixelCenter + float2(0.0, nearRadius);
    float param_33 = 0.100000001490116119384765625;
    bool param_34 = atlasText;
    bool param_35 = renderUnderlay;
    bool param_36 = renderForeground;
    bool param_37 = renderGlow;
    float3 param_38 = premultiplied;
    float param_39 = alpha;
    float param_40 = weightSum;
    accumulate_text_sample(param_32, param_33, param_34, param_35, param_36, param_37, param_38, param_39, param_40, pc, fontAtlas);
    premultiplied = param_38;
    alpha = param_39;
    weightSum = param_40;
    float2 param_41 = pixelCenter + float2(0.0, -nearRadius);
    float param_42 = 0.100000001490116119384765625;
    bool param_43 = atlasText;
    bool param_44 = renderUnderlay;
    bool param_45 = renderForeground;
    bool param_46 = renderGlow;
    float3 param_47 = premultiplied;
    float param_48 = alpha;
    float param_49 = weightSum;
    accumulate_text_sample(param_41, param_42, param_43, param_44, param_45, param_46, param_47, param_48, param_49, pc, fontAtlas);
    premultiplied = param_47;
    alpha = param_48;
    weightSum = param_49;
    float2 param_50 = pixelCenter + float2(midRadius);
    float param_51 = 0.070000000298023223876953125;
    bool param_52 = atlasText;
    bool param_53 = renderUnderlay;
    bool param_54 = renderForeground;
    bool param_55 = renderGlow;
    float3 param_56 = premultiplied;
    float param_57 = alpha;
    float param_58 = weightSum;
    accumulate_text_sample(param_50, param_51, param_52, param_53, param_54, param_55, param_56, param_57, param_58, pc, fontAtlas);
    premultiplied = param_56;
    alpha = param_57;
    weightSum = param_58;
    float2 param_59 = pixelCenter + float2(-midRadius, midRadius);
    float param_60 = 0.070000000298023223876953125;
    bool param_61 = atlasText;
    bool param_62 = renderUnderlay;
    bool param_63 = renderForeground;
    bool param_64 = renderGlow;
    float3 param_65 = premultiplied;
    float param_66 = alpha;
    float param_67 = weightSum;
    accumulate_text_sample(param_59, param_60, param_61, param_62, param_63, param_64, param_65, param_66, param_67, pc, fontAtlas);
    premultiplied = param_65;
    alpha = param_66;
    weightSum = param_67;
    float2 param_68 = pixelCenter + float2(midRadius, -midRadius);
    float param_69 = 0.070000000298023223876953125;
    bool param_70 = atlasText;
    bool param_71 = renderUnderlay;
    bool param_72 = renderForeground;
    bool param_73 = renderGlow;
    float3 param_74 = premultiplied;
    float param_75 = alpha;
    float param_76 = weightSum;
    accumulate_text_sample(param_68, param_69, param_70, param_71, param_72, param_73, param_74, param_75, param_76, pc, fontAtlas);
    premultiplied = param_74;
    alpha = param_75;
    weightSum = param_76;
    float2 param_77 = pixelCenter + float2(-midRadius, -midRadius);
    float param_78 = 0.070000000298023223876953125;
    bool param_79 = atlasText;
    bool param_80 = renderUnderlay;
    bool param_81 = renderForeground;
    bool param_82 = renderGlow;
    float3 param_83 = premultiplied;
    float param_84 = alpha;
    float param_85 = weightSum;
    accumulate_text_sample(param_77, param_78, param_79, param_80, param_81, param_82, param_83, param_84, param_85, pc, fontAtlas);
    premultiplied = param_83;
    alpha = param_84;
    weightSum = param_85;
    float2 param_86 = pixelCenter + float2(farRadius, 0.0);
    float param_87 = 0.0350000001490116119384765625;
    bool param_88 = atlasText;
    bool param_89 = renderUnderlay;
    bool param_90 = renderForeground;
    bool param_91 = renderGlow;
    float3 param_92 = premultiplied;
    float param_93 = alpha;
    float param_94 = weightSum;
    accumulate_text_sample(param_86, param_87, param_88, param_89, param_90, param_91, param_92, param_93, param_94, pc, fontAtlas);
    premultiplied = param_92;
    alpha = param_93;
    weightSum = param_94;
    float2 param_95 = pixelCenter + float2(-farRadius, 0.0);
    float param_96 = 0.0350000001490116119384765625;
    bool param_97 = atlasText;
    bool param_98 = renderUnderlay;
    bool param_99 = renderForeground;
    bool param_100 = renderGlow;
    float3 param_101 = premultiplied;
    float param_102 = alpha;
    float param_103 = weightSum;
    accumulate_text_sample(param_95, param_96, param_97, param_98, param_99, param_100, param_101, param_102, param_103, pc, fontAtlas);
    premultiplied = param_101;
    alpha = param_102;
    weightSum = param_103;
    float2 param_104 = pixelCenter + float2(0.0, farRadius);
    float param_105 = 0.0350000001490116119384765625;
    bool param_106 = atlasText;
    bool param_107 = renderUnderlay;
    bool param_108 = renderForeground;
    bool param_109 = renderGlow;
    float3 param_110 = premultiplied;
    float param_111 = alpha;
    float param_112 = weightSum;
    accumulate_text_sample(param_104, param_105, param_106, param_107, param_108, param_109, param_110, param_111, param_112, pc, fontAtlas);
    premultiplied = param_110;
    alpha = param_111;
    weightSum = param_112;
    float2 param_113 = pixelCenter + float2(0.0, -farRadius);
    float param_114 = 0.0350000001490116119384765625;
    bool param_115 = atlasText;
    bool param_116 = renderUnderlay;
    bool param_117 = renderForeground;
    bool param_118 = renderGlow;
    float3 param_119 = premultiplied;
    float param_120 = alpha;
    float param_121 = weightSum;
    accumulate_text_sample(param_113, param_114, param_115, param_116, param_117, param_118, param_119, param_120, param_121, pc, fontAtlas);
    premultiplied = param_119;
    alpha = param_120;
    weightSum = param_121;
    alpha /= fast::max(weightSum, 9.9999997473787516355514526367188e-05);
    premultiplied /= float3(fast::max(weightSum, 9.9999997473787516355514526367188e-05));
    float3 _2303;
    if (alpha > 9.9999997473787516355514526367188e-05)
    {
        _2303 = premultiplied / float3(alpha);
    }
    else
    {
        _2303 = float3(0.0);
    }
    float3 rgb = _2303;
    return float4(rgb, alpha);
}

kernel void text_msdf_comp(constant Renderer2DConstants& pc [[buffer(0)]], texture2d<float> fontAtlas [[texture(0)]], texture2d<float, access::read_write> colorBuffer [[texture(1)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    int2 localPixel = int2(gl_GlobalInvocationID.xy);
    int2 exactExtent = dispatch_extent(pc);
    bool _2334 = localPixel.x >= exactExtent.x;
    bool _2343;
    if (!_2334)
    {
        _2343 = localPixel.y >= exactExtent.y;
    }
    else
    {
        _2343 = _2334;
    }
    if (_2343)
    {
        return;
    }
    int2 pixel = dispatch_origin(pc) + localPixel;
    int2 screenSize = int2(colorBuffer.get_width(), colorBuffer.get_height());
    bool _2357 = pixel.x < 0;
    bool _2364;
    if (!_2357)
    {
        _2364 = pixel.y < 0;
    }
    else
    {
        _2364 = _2357;
    }
    bool _2373;
    if (!_2364)
    {
        _2373 = pixel.x >= screenSize.x;
    }
    else
    {
        _2373 = _2364;
    }
    bool _2382;
    if (!_2373)
    {
        _2382 = pixel.y >= screenSize.y;
    }
    else
    {
        _2382 = _2373;
    }
    if (_2382)
    {
        return;
    }
    float2 p = float2(pixel) + float2(0.5);
    float glyphPad = fast::max(fast::max(pc.effect0.y, pc.effect0.z), pc.effect1.w);
    float shadowPad = fast::max(abs(pc.effect1.x), abs(pc.effect1.y)) + fast::max(pc.effect1.z, 0.0);
    float pad = fast::max(glyphPad, shadowPad) + 2.0;
    float samplePad = pad + 1.0;
    bool _2425 = p.x < (pc.rect.x - samplePad);
    bool _2436;
    if (!_2425)
    {
        _2436 = p.y < (pc.rect.y - samplePad);
    }
    else
    {
        _2436 = _2425;
    }
    bool _2450;
    if (!_2436)
    {
        _2450 = p.x > ((pc.rect.x + pc.rect.z) + samplePad);
    }
    else
    {
        _2450 = _2436;
    }
    bool _2464;
    if (!_2450)
    {
        _2464 = p.y > ((pc.rect.y + pc.rect.w) + samplePad);
    }
    else
    {
        _2464 = _2450;
    }
    if (_2464)
    {
        return;
    }
    bool atlasText = (pc.data.y & 256u) != 0u;
    uint textPass = pc.data.z & 3u;
    bool renderUnderlay = (textPass == 0u) || (textPass == 1u);
    bool renderForeground = (textPass == 0u) || (textPass == 2u);
    bool _2497;
    if (renderUnderlay)
    {
        _2497 = (pc.data.y & 16u) != 0u;
    }
    else
    {
        _2497 = renderUnderlay;
    }
    bool renderGlow = _2497;
    float2 param = p;
    bool param_1 = atlasText;
    bool param_2 = renderUnderlay;
    bool param_3 = renderForeground;
    bool param_4 = renderGlow;
    float4 src = resolve_blurred_text(param, param_1, param_2, param_3, param_4, pc, fontAtlas);
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

