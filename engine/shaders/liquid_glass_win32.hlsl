// Windows Liquid Glass optical pass. Squircle geometry and analytical corner
// normals are adapted from LiquidGlassWinUI (MIT; see
// engine/licences/liquid-glass-winui.txt). The DWM renderer must bind the
// sharp live backdrop to t0 and the two-pass Gaussian result to t1.
// Acrylic/Mica output is not a valid input.
Texture2D sharpBackdrop : register(t0);
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
    // Most lens pixels lie along a straight side or inside the body. Keep
    // their trivial norm out of the logarithm/exponential corner path.
    [branch]
    if (outside.x != 0.0 && outside.y != 0.0)
        norm = pow(pow(outside.x, n) + pow(outside.y, n), 1.0 / n);
    // Keep negative coordinates signed inside the corner's central square.
    // Taking abs(q) here carved four holes and a seam inside a growing lens.
    return lerp(length(outside), norm, amount) +
        min(max(q.x, q.y), 0.0) - radius;
}

float box_distance(float2 p, float2 halfSize)
{
    float2 q = abs(p) - halfSize;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

// Match the sharp renderer's liquid merge: the source cap extends left inside
// the main island, and a polynomial smooth union joins the detached circle.
float liquid_bridge_distance(float2 p, float2 halfSize, float amount)
{
    float radius = max(halfSize.y, 0.5);
    float centreOffset = max(halfSize.x - radius, 0.0);
    float2 source = p - float2(-centreOffset, 0.0);
    float left = length(float2(max(source.x, 0.0), source.y)) - radius;
    float right = length(p - float2(centreOffset, 0.0)) - radius;
    float centreDistance = centreOffset * 2.0;
    float surfaceGap = max(centreDistance - radius * 2.0, 0.0);
    float easedAmount = smoothstep(0.0, 1.0, saturate(amount));
    float separationFactor = smoothstep(0.0, radius * 0.9, centreDistance);
    float smoothing = lerp(surfaceGap * 2.0 + 0.5,
        surfaceGap * 2.0 + radius * 0.95, easedAmount) * separationFactor;
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

// This follows the island renderer's squircle and dynamic notch equations.
// Shape choices are uniform across the draw. Explicit branches stop FXC
// evaluating the bridge or notch calculations for an ordinary rounded lens.
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
        // The sharp merge's source cap continues left, but its own dispatch
        // bounds clip that continuation. Preserve those bounds inside a union.
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
    if (hasExclusion != 0)
        distance = max(distance, -contour_distance(pixelPosition, exclusion));
    return distance;
}

float2 surface_normal(float2 pixelPosition)
{
    // Derive the rim and refraction direction from the complete contour,
    // including flattened corners and the notch's concave flares.
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
    float coverage = saturate(0.5 - distance);
    if (coverage <= 0.0)
        return float4(0.0, 0.0, 0.0, 0.0);

    float band = saturate(1.0 + distance / max(edgeThickness, 1.0));
    // Interior pixels have no displacement. Beyond sixteen pixels the rim's
    // Gaussian underflows to zero even before multiplication; avoid four
    // additional contour evaluations there without changing the result.
    float2 normal = float2(0.0, -1.0);
    [branch]
    if (band > 0.0 || distance > -16.0)
        normal = surface_normal(pixelPosition);
    float bend = refractionStrength * band * band;
    float2 backdropUv = backdropUvOrigin + input.uv * backdropUvScale;
    float2 refractedUv = backdropUv - normal * bend * backdropTexelSize;
    float2 dispersion = normal * dispersionStrength * band *
        backdropTexelSize;

    // Scratch textures contain one mip. Explicit LOD keeps conditional taps
    // independent of implicit derivatives across the lens's edge branches.
    float4 sharpSample = sharpBackdrop.SampleLevel(backdropSampler, refractedUv, 0.0);
    float3 centre = sharpSample.rgb;
    float frost = saturate(blurRadius / 16.0);
    float3 refracted = centre;
    [branch]
    if (band > 0.0 && dispersionStrength != 0.0 && frost < 1.0)
    {
        refracted.r = sharpBackdrop.SampleLevel(backdropSampler, refractedUv + dispersion, 0.0).r;
        refracted.b = sharpBackdrop.SampleLevel(backdropSampler, refractedUv - dispersion, 0.0).b;
    }
    // Frost the complete live background, including the centre of the lens.
    // Zero gives clear refraction; the maximum gives the full Gaussian pass.
    float3 colour = refracted;
    [branch]
    if (frost > 0.0)
        colour = lerp(refracted,
            blurredBackdrop.SampleLevel(backdropSampler, refractedUv, 0.0).rgb, frost);
    // Only completely empty input needs the transparent fallback. A dim blur
    // is still valid: fading it exposes the original sharp text underneath.
    // Test both existing samples, including dark blur tails and sharp glyphs;
    // DWM may leave source alpha unused, so it cannot establish availability.
    float sourceAvailable = (any(centre != 0.0) || any(colour != 0.0)) ? 1.0 : 0.0;
    // Refraction can move a bright glyph onto valid black background. Writing
    // transparent black there reveals the original, unbent glyph in the target.
    // Only this empty edge case needs the undisplaced sample; ordinary pixels
    // retain the existing taps, and genuinely unavailable input stays clear.
    [branch]
    if (sourceAvailable == 0.0 && bend != 0.0)
        sourceAvailable = any(sharpBackdrop.SampleLevel(
            backdropSampler, backdropUv, 0.0).rgb != 0.0) ? 1.0 : 0.0;
    // Tint is an independent translucent layer. Masking it with source
    // availability left untinted patches; forcing full opacity hid the desktop.
    float tintAmount = saturate(tintStrength);
    float materialOpacity = lerp(sourceAvailable, 1.0, tintAmount);
    float3 materialColour = lerp(colour * sourceAvailable, tintColour, tintAmount);

    float fresnel = 0.0;
    [branch]
    if (distance > -16.0)
    {
        float angle = atan2(normal.y, normal.x);
        float travellingLight = pow(saturate(0.5 + 0.5 *
            cos(angle - lightPhase)), 12.0);
        float edgeDistance = max(-distance, 0.0);
        float brightCore = exp(-edgeDistance * edgeDistance / 2.25);
        fresnel = brightCore * (0.25 + 0.75 * travellingLight);
    }
    // An unavailable background must not introduce a grey outline either.
    materialColour += rimStrength * 0.65 * fresnel * sourceAvailable;
    if ((paintCapGradient & 1u) != 0)
    {
        float fraction = pixelPosition.y / max(panelSize.y, 1.0);
        float progress = saturate((fraction - capGradient.z) /
            max(capGradient.w - capGradient.z, 0.0001));
        float3 capColour = float3((paintCapGradient >> 8u) & 255u,
            (paintCapGradient >> 16u) & 255u, (paintCapGradient >> 24u) & 255u) / 255.0;
        float capOpacity = saturate(lerp(capGradient.x, capGradient.y, progress));
        materialColour = lerp(materialColour, capColour, capOpacity);
        materialOpacity = lerp(materialOpacity, 1.0, capOpacity);
    }
    // The existing optical blend state expects straight colour; preserve the
    // independently composed material alpha when converting back from premultiplied.
    // DWM can use scRGB render targets, so retain values above 1.0.
    return float4(max(materialColour / max(materialOpacity, 0.00001), 0.0),
        coverage * opacity * viewportOpacity * materialOpacity);
}
