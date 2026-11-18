// Copyright 2026 59xa
// Portions Copyright (c) 2026 luckyelysia
// SPDX-License-Identifier: Apache-2.0 AND MIT

// Squircle geometry and analytical corner normals are adapted from
// LiquidGlassWinUI (MIT; see engine/licences/liquid-glass-winui.txt).
Texture2D sharpBackdrop : register(t0);
#include "../include/vibranceUI/graphics/liquid_glass_refraction.h"
Texture2D blurredBackdrop : register(t1);
SamplerState backdropSampler : register(s0);

struct ContourParameters
{
    float4 rect;
    float4 radii;
    float4 params;
    float4 kind;
};

cbuffer LiquidGlassParameters : register(b0)
{
    float2 panelSize;
    float2 backdropTexelSize;
    float cornerRadius;
    float edgeThickness;
    float refractionStrength;
    float dispersionStrength;
    float blurRadius;
    float rimStrength;
    float lightPhase;
    float opacity;
    float cornerExponent;
    float3 tintColour;
    float2 backdropUvOrigin;
    float2 backdropUvScale;
    float4 cornerRadii;
    float squircleAmount;
    float notchAmount;
    float notchDepth;
    float shape;
    float2 panelOrigin;
    float2 drawSize;
    float tintStrength;
    uint contourCount;
    uint hasExclusion;
    uint paintCapGradient;
    float4 capGradient;
    ContourParameters contours[3];
    ContourParameters exclusion;
    float4 viewportRect;
    float4 viewportFade;
};

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float corner_radius(float2 p, float4 radii)
{
    return p.y < 0.0 ? (p.x < 0.0 ? radii.x : radii.y) :
        (p.x < 0.0 ? radii.w : radii.z);
}

float squircle_distance(float2 p, float2 halfSize, float4 radii,
    float amount, float exponent)
{
    float radius = clamp(corner_radius(p, radii), 0.0,
        min(halfSize.x, halfSize.y));
    float2 q = abs(p) - max(halfSize - radius, 0.0);
    float2 outside = max(q, 0.0);
    float n = lerp(2.0, clamp(exponent, 2.0, 5.0), amount);
    float norm = outside.x + outside.y;


    [branch]
    if (outside.x != 0.0 && outside.y != 0.0)
        norm = pow(pow(outside.x, n) + pow(outside.y, n), 1.0 / n);


    return lerp(length(outside), norm, amount) +
        min(max(q.x, q.y), 0.0) - radius;
}

float box_distance(float2 p, float2 halfSize)
{
    float2 q = abs(p) - halfSize;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}


float liquid_bridge_distance(float2 p, float2 halfSize, float amount)
{
    float radius = max(halfSize.y, 0.5);
    float centreOffset = max(halfSize.x - radius, 0.0);
    float2 source = p - float2(-centreOffset, 0.0);
    float left = length(source) - radius;
    float right = length(p - float2(centreOffset, 0.0)) - radius;
    float centreDistance = centreOffset * 2.0;
    float surfaceGap = max(centreDistance - radius * 2.0, 0.0);
    float easedAmount = smoothstep(0.0, 1.0, saturate(amount));
    float separationFactor = smoothstep(0.0, radius * 0.9, centreDistance);
    float proximity = 1.0 - smoothstep(0.0, radius * 0.9, surfaceGap);
    float neckBand = 1.0 - smoothstep(radius * 0.65, radius, abs(p.y));
    float smoothing = radius * 1.6 * easedAmount * separationFactor *
        proximity * neckBand;
    float distance = min(left, right);
    if (smoothing > 0.001)
    {
        float blend = saturate(0.5 + 0.5 * (right - left) / smoothing);
        distance = lerp(right, left, blend) - smoothing * blend * (1.0 - blend);
    }
    return distance;
}

float notch_distance(float2 p, float2 halfSize, float2 size,
    float4 parameters, float2 kind, float amount)
{
    float notch = saturate(parameters.z);
    float radius = clamp(kind.y, 0.0, min(halfSize.x, halfSize.y));
    float topRadius = radius * saturate(1.0 - notch / 0.85);
    float body = squircle_distance(p, halfSize,
        float4(topRadius, topRadius, radius, radius), max(amount, notch), parameters.y);
    float depth = parameters.w > 0.5 ? parameters.w :
        clamp(size.y * 0.62, 0.0, 92.0);
    float progress = saturate((notch - 0.85) / 0.15);
    float flareTarget = clamp(depth * 0.18, 0.0, halfSize.y);
    float flareT = saturate(halfSize.y / max(flareTarget, 0.001)) * progress;
    float flare = flareTarget * flareT * flareT;
    if (flare <= 0.05) return body;
    float2 topCentre = float2(0.0, -halfSize.y + flare * 0.5);
    float topBar = box_distance(p - topCentre,
        float2(halfSize.x + flare, flare * 0.5));
    float leftCutout = length(p - float2(-halfSize.x - flare,
        -halfSize.y + flare)) - flare;
    float rightCutout = length(p - float2(halfSize.x + flare,
        -halfSize.y + flare)) - flare;
    return min(body, max(topBar, max(-leftCutout, -rightCutout)));
}




float shape_distance(float2 pixelPosition, float2 size, float4 radii,
    float4 parameters, float2 kind)
{
    float2 halfSize = max(size * 0.5, 0.5);
    float2 p = pixelPosition - halfSize;
    float distance = 0.0;
    [branch]
    if (kind.x > 4.5)
        distance = liquid_bridge_distance(p, halfSize, parameters.z);
    else
    {
        [branch]
        if (kind.x < 0.5)
            distance = box_distance(p, halfSize);
        else
        {
            [branch]
            if (kind.x > 1.5 && kind.x < 2.5)
                distance = (length(p / halfSize) - 1.0) * min(halfSize.x, halfSize.y);
            else
            {
                float amount = kind.x > 2.5 ? saturate(parameters.x) : 0.0;
                [branch]
                if (kind.x < 3.5 || parameters.z <= 0.01)
                    distance = squircle_distance(p, halfSize, radii, amount, parameters.y);
                else
                    distance = notch_distance(p, halfSize, size, parameters, kind, amount);
            }
        }
    }
    return distance;
}

float contour_distance(float2 pixelPosition, ContourParameters contour)
{
    float2 localPosition = pixelPosition - contour.rect.xy;
    float distance = shape_distance(localPosition, contour.rect.zw,
        contour.radii, contour.params, contour.kind.xy);
    if (contour.kind.x > 4.5)
    {

        float2 halfSize = max(contour.rect.zw * 0.5, 0.5);
        distance = max(distance, box_distance(localPosition - halfSize, halfSize));
    }
    return distance;
}

float rounded_box_distance(float2 pixelPosition)
{
    float distance = 1e20;
    if (contourCount == 0)
    {
        distance = shape_distance(pixelPosition, panelSize, cornerRadii,
            float4(squircleAmount, cornerExponent, notchAmount, notchDepth),
            float2(shape, cornerRadius));
    }
    else
    {
        [unroll]
        for (uint index = 0; index < 3; ++index)
            if (index < contourCount)
                distance = min(distance, contour_distance(pixelPosition, contours[index]));
    }
    return distance;
}

float2 surface_normal(float2 pixelPosition)
{



    float2 normal = float2(
        rounded_box_distance(pixelPosition + float2(0.5, 0.0)) -
        rounded_box_distance(pixelPosition - float2(0.5, 0.0)),
        rounded_box_distance(pixelPosition + float2(0.0, 0.5)) -
        rounded_box_distance(pixelPosition - float2(0.0, 0.5)));
    if (dot(normal, normal) < 0.0001)
    {
        normal = float2(0.0, -1.0);
    }
    return normal * rsqrt(max(dot(normal, normal), 0.0001));
}

float3 sample_glass(float2 uv, float2 normal, float footprint)
{
    float3 result = 0.0;


    [branch] if (blurRadius <= 0.0 || (refractionStrength == 0.0 && blurRadius <= 0.5))
        result = sharpBackdrop.SampleLevel(backdropSampler, uv, 0.0).rgb;
    else
    {
        float3 wide = blurredBackdrop.SampleLevel(backdropSampler, uv, 0.0).rgb;
        float diffusion = smoothstep(0.5, 18.0, blurRadius);
        result = wide;
        [branch] if (diffusion < 0.999)
        {
            float2 across = normal * (0.5 * footprint) * backdropTexelSize;
            float2 along = float2(-normal.y, normal.x) * 0.5 * backdropTexelSize;
            float3 detail = (sharpBackdrop.SampleLevel(backdropSampler, uv + across + along, 0.0).rgb +
                sharpBackdrop.SampleLevel(backdropSampler, uv + across - along, 0.0).rgb +
                sharpBackdrop.SampleLevel(backdropSampler, uv - across + along, 0.0).rgb +
                sharpBackdrop.SampleLevel(backdropSampler, uv - across - along, 0.0).rgb) * 0.25;
            if ((paintCapGradient & 2u) == 0u)
                detail = lerp(detail, min(detail, wide), 0.70);
            result = lerp(detail, wide, diffusion);
        }
    }
    return result;
}

float3 liquid_tone(float3 colour)
{
    float luminance = dot(colour, float3(0.2126, 0.7152, 0.0722));
    float bounded = saturate(luminance);
    float mapped = 0.125 + bounded * (0.99 - 0.39 * bounded) + max(luminance - 1.0, 0.0) * 0.21;
    if (mapped < luminance) return colour * (mapped / max(luminance, 0.0001));
    return colour + max(1.0 - colour, 0.0) * ((mapped - luminance) / max(1.0 - luminance, 0.0001));
}

float4 main(PixelInput input) : SV_Target
{
    float2 pixelPosition = input.uv * drawSize - panelOrigin;
    float viewportOpacity = 1.0;
    if (viewportFade.w != 0.0)
    {
        float2 offset = pixelPosition - viewportRect.xy;
        if (offset.x < 0.0 || offset.y < 0.0 || offset.x >= viewportRect.z || offset.y >= viewportRect.w)
            return float4(0.0, 0.0, 0.0, 0.0);
        if (viewportFade.x > 0.0) viewportOpacity = min(viewportOpacity, smoothstep(0.0, viewportFade.x, offset.y));
        if (viewportFade.y > 0.0) viewportOpacity = min(viewportOpacity, smoothstep(0.0, viewportFade.y, viewportRect.w - offset.y));
        viewportOpacity = lerp(saturate(viewportFade.z), 1.0, viewportOpacity);
    }
    float distance = rounded_box_distance(pixelPosition);
    float coverageDistance = distance;
    if (hasExclusion != 0)
        coverageDistance = max(coverageDistance, -contour_distance(pixelPosition, exclusion));
    float coverage = saturate(0.5 - coverageDistance);
    if (coverage <= 0.0)
        return float4(0.0, 0.0, 0.0, 0.0);

    float capOpacity = 0.0;
    float3 capColour = 0.0;
    if ((paintCapGradient & 1u) != 0)
    {
        float fraction = pixelPosition.y / max(panelSize.y, 1.0);
        float progress = saturate((fraction - capGradient.z) /
            max(capGradient.w - capGradient.z, 0.0001));
        capColour = float3((paintCapGradient >> 8u) & 255u,
            (paintCapGradient >> 16u) & 255u, (paintCapGradient >> 24u) & 255u) / 255.0;
        capOpacity = saturate(lerp(capGradient.x, capGradient.y, progress));
        [branch]
        if (capOpacity == 1.0)
        {


            return float4(capColour, coverage * opacity * viewportOpacity);
        }
    }
    float rimVisibility = 1.0 - capOpacity;
    float frost = sqrt(saturate(blurRadius / 30.0));
    float shortSide = min(panelSize.x, panelSize.y);
    float rimWidth = clamp(shortSide / 96.0, 0.75, 1.5);


    float2 normal = float2(0.0, -1.0);
    [branch]
    if ((glass_lens_band(distance, edgeThickness, panelSize.x, panelSize.y, 1.0) > 0.0 && (refractionStrength != 0.0 ||
            (dispersionStrength != 0.0 && frost < 1.0))) ||
        (distance > -5.0 * rimWidth && rimStrength > 0.0 && rimVisibility > 0.001))
        normal = surface_normal(pixelPosition);
    float band = glass_lens_band(distance, edgeThickness, panelSize.x, panelSize.y, normal.x);
    float2 lensOffset;
    glass_lens_offset(distance, edgeThickness, refractionStrength,
        normal.x, normal.y, pixelPosition.x, pixelPosition.y, panelSize.x, panelSize.y,
        lensOffset.x, lensOffset.y);
    float2 backdropUv = backdropUvOrigin + input.uv * backdropUvScale;
    float2 refractedUv = backdropUv + lensOffset * backdropTexelSize;
    float2 opticalNormal = dot(lensOffset, lensOffset) > 0.0001 ? -normalize(lensOffset) : normal;
    float footprint = blurRadius > 0.0 && band > 0.0 ? glass_lens_footprint(
        distance, edgeThickness, refractionStrength, normal.x, normal.y,
        pixelPosition.x, pixelPosition.y, panelSize.x, panelSize.y) : 1.0;
    float2 dispersion = opticalNormal * min(dispersionStrength, 0.65) * band *
        (1.0 - frost * 0.75) * backdropTexelSize;



    float3 centre = sample_glass(refractedUv, opticalNormal, footprint);
    float3 refracted = centre;
    [branch]
    if (band > 0.0 && dispersionStrength != 0.0 && frost < 1.0)
    {
        refracted.r = sample_glass(refractedUv + dispersion, opticalNormal, footprint).r;
        refracted.b = sample_glass(refractedUv - dispersion, opticalNormal, footprint).b;
    }


    float3 colour = (paintCapGradient & 2u) == 0u && (refractionStrength > 0.0 || blurRadius > 0.5) ?
        liquid_tone(refracted) : refracted;
    float tintAmount = saturate(tintStrength);



    float rimHighlight = 0.0;
    float rimShade = 0.0;
    float3 rimColour = 1.0;
    [branch]
    if (distance > -5.0 * rimWidth && rimStrength > 0.0 && rimVisibility > 0.001)
    {
        float light = lightPhase < 0.0 ? 1.0 : 0.7 + 0.3 *
            cos(lightPhase + pixelPosition.x / max(panelSize.x, 1.0) * 3.14159265 + normal.y * 0.8);
        float edgeDistance = distance / rimWidth;
        float brightCore = exp(-(edgeDistance + 0.35) * (edgeDistance + 0.35) / 0.55);
        float soft = exp(-(edgeDistance + 1.4) * (edgeDistance + 1.4) / 3.0);
        float vertical = pow(abs(normal.y), 3.5);
        rimShade = rimStrength * 0.32 * pow(abs(normal.x), 3.0) *
            exp(-(edgeDistance + 0.15) * (edgeDistance + 0.15) / 0.40);
        rimHighlight = rimStrength * (0.17 * brightCore + 0.018 * soft) * vertical * light;
        [branch]
        if (rimHighlight > 0.001)
        {
            float reach = min(shortSide * 1.6, 120.0);
            float spread = min(shortSide * 0.65, 40.0);
            float2 tangent = float2(-normal.y, normal.x);
            float3 nearby = 0.0;
            [unroll] for (int row = 0; row < 3; ++row)
            {
                float radialWeight = row == 1 ? 0.5 : 0.25;
                float2 centre = backdropUv + normal * reach * (0.15 + 0.35 * row) * backdropTexelSize;
                [unroll] for (int column = 0; column < 3; ++column)
                {
                    float weight = radialWeight * (column == 1 ? 0.5 : 0.25);
                    nearby += sharpBackdrop.SampleLevel(backdropSampler,
                        centre + tangent * spread * (column - 1) * backdropTexelSize, 0.0).rgb * weight;
                }
            }
            float energy = max(nearby.r, max(nearby.g, nearby.b));
            float reflectionBrightness = lerp(0.85, 1.0, smoothstep(0.0, 0.65, energy));


            rimColour = lerp(float3(1.0, 1.0, 1.0), nearby / max(energy, 0.0001),
                smoothstep(0.0, 0.18, energy) * 0.95) * reflectionBrightness;
            float tintChroma = max(tintColour.r, max(tintColour.g, tintColour.b)) -
                min(tintColour.r, min(tintColour.g, tintColour.b));
            rimColour = lerp(rimColour, saturate(tintColour * 1.12 + 0.08),
                tintAmount * 0.82 * saturate(tintChroma * 1.5));
        }
    }
    float3 materialColour = lerp(colour, tintColour, tintAmount);
    if ((paintCapGradient & 1u) != 0)
    {


        materialColour = lerp(materialColour, capColour, capOpacity);
    }
    materialColour *= 1.0 - rimShade * rimVisibility;
    float3 highlightTarget = lerp(2.2.xxx, saturate(tintColour * 1.12 + 0.08),
        tintAmount * saturate((max(tintColour.r, max(tintColour.g, tintColour.b)) -
            min(tintColour.r, min(tintColour.g, tintColour.b))) * 1.5));
    materialColour += max(highlightTarget - materialColour, 0.0) * rimColour * rimHighlight * rimVisibility;

    return float4(max(materialColour, 0.0), coverage * opacity * viewportOpacity);
}
