#pragma once
#include "vibranceUI/export.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vibranceUI/graphics/backdrop.h>

enum class Renderer2DPrimitive : uint32_t
{
    // Shapes share one shader path, with primitive selecting the signed-distance test
    eClear = 0,
    eRectangle = 1,
    eRoundedRectangle = 2,
    eEllipse = 3,
    eGlyphRun = 4,
    eMedia = 5,
    eSquircle = 6,
    eNotchedSquircle = 7,
    eCircularProgress = 8
};

enum class Renderer2DFill : uint32_t
{
    eNone = 0,
    eSolid = 1,
    eLinearGradient = 2,
    eRadialGradient = 3
};

enum class Renderer2DCacheMode : uint32_t
{
    // Static is cheapest; dynamic is for moving controls; timed wakes only briefly
    eStatic = 0,
    eDynamic = 1,
    eTimed = 2
};

enum class DisplayTransitionCurve2D : uint32_t
{
    // Curves mirror common UI animation vocabulary used by the builder helpers
    eLinear = 0,
    eEaseOut = 1,
    eEaseInOut = 2,
    eDefault = 3,
    eEaseIn = 4,
    eInterpolatingSpring = 5,
    eInteractiveSpring = 6,
    eSpring = 7,
    eTimingCurve = 8
};

enum class Media2DSourceType : uint32_t
{
    eUnknown = 0,
    eRasterImage = 1,
    eSvg = 2,
    eVideo = 3
};

enum class Media2DFit : uint32_t
{
    // Fit controls how image media occupies its layout rectangle
    eStretch = 0,
    eContain = 1,
    eCover = 2
};

enum Renderer2DStyleFlags : uint32_t
{
    // Packed flags tell compute shaders which optional style branches to run
    eRenderer2DStyleNone = 0,
    eRenderer2DStyleGradient = 1u << 0,
    eRenderer2DStyleOutline = 1u << 1,
    eRenderer2DStyleSdfEdges = 1u << 2,
    eRenderer2DStyleShadow = 1u << 3,
    eRenderer2DStyleGlow = 1u << 4,
    eRenderer2DStyleBlur = 1u << 5,
    eRenderer2DStyleMsdfText = 1u << 6,
    eRenderer2DStyleClear = 1u << 7,
    eRenderer2DStyleAtlasText = 1u << 8,
    eRenderer2DStyleBlurComposite = 1u << 9,
    eRenderer2DStyleMedia = 1u << 10,
    eRenderer2DStyleRadialGradient = 1u << 11,
    eRenderer2DStyleTransform2_5D = 1u << 12,
    eRenderer2DStyleMediaColorAdjust = 1u << 13,
    eRenderer2DStyleMediaAutoBlackLift = 1u << 14,
    eRenderer2DStyleShapeMask = 1u << 15,
    eRenderer2DStyleCornerRadii = 1u << 16,
    eRenderer2DStyleBlurHorizontal = 1u << 17,
    eRenderer2DStyleBlurVertical = 1u << 18,
    eRenderer2DStyleMediaTintAsMask = 1u << 19,
    eRenderer2DStyleBlurFollowsFillAlpha = 1u << 20,
    eRenderer2DStyleBlurInheritedShapeMask = 1u << 21,
    eRenderer2DStyleMediaPremultipliedAlpha = 1u << 22,
    eRenderer2DStyleShadowOutsideOnly = 1u << 23
};

inline std::optional<uint32_t> renderer2d_hex_digit(char value)
{
    // Colour parsing accepts both upper and lower case hexadecimal values
    if (value >= '0' && value <= '9')
    {
        return static_cast<uint32_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f')
    {
        return static_cast<uint32_t>(value - 'a' + 10);
    }
    if (value >= 'A' && value <= 'F')
    {
        return static_cast<uint32_t>(value - 'A' + 10);
    }
    return std::nullopt;
}

inline std::optional<uint32_t> renderer2d_hex_byte(std::string_view value)
{
    if (value.size() != 2u)
    {
        return std::nullopt;
    }

    const std::optional<uint32_t> high = renderer2d_hex_digit(value[0]);
    const std::optional<uint32_t> low = renderer2d_hex_digit(value[1]);
    if (!high || !low)
    {
        return std::nullopt;
    }
    return (*high << 4u) | *low;
}

inline bool renderer2d_color_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

inline std::string_view renderer2d_trim_color_token(std::string_view value)
{
    while (!value.empty() && renderer2d_color_space(value.front()))
    {
        value.remove_prefix(1u);
    }
    while (!value.empty() && renderer2d_color_space(value.back()))
    {
        value.remove_suffix(1u);
    }
    return value;
}

inline char renderer2d_lower_ascii(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool renderer2d_starts_with_i(std::string_view value, std::string_view prefix)
{
    if (value.size() < prefix.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i)
    {
        if (renderer2d_lower_ascii(value[i]) != renderer2d_lower_ascii(prefix[i]))
        {
            return false;
        }
    }
    return true;
}

struct Renderer2DColorNumber
{
    float value = 0.0f;
    bool percent = false;
};

inline std::optional<Renderer2DColorNumber> renderer2d_parse_color_number(std::string_view value)
{
    value = renderer2d_trim_color_token(value);
    if (value.empty())
    {
        return std::nullopt;
    }

    std::string token(value);
    char* end = nullptr;
    const float parsed = std::strtof(token.c_str(), &end);
    if (end == token.c_str())
    {
        return std::nullopt;
    }

    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
    {
        ++end;
    }

    bool percent = false;
    if (*end == '%')
    {
        percent = true;
        ++end;
    }

    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
    {
        ++end;
    }

    if (*end != '\0')
    {
        return std::nullopt;
    }

    return Renderer2DColorNumber {
        percent ? parsed * 0.01f : parsed,
        percent
    };
}

inline void renderer2d_append_rgba_component(
    std::vector<Renderer2DColorNumber>& components,
    std::string_view token)
{
    if (const std::optional<Renderer2DColorNumber> parsed = renderer2d_parse_color_number(token))
    {
        components.push_back(*parsed);
    }
}

inline std::optional<glm::vec4> renderer2d_parse_rgba_function_color(std::string_view value)
{
    // Accept CSS-like rgb and rgba tokens for theme and app-authored colours
    value = renderer2d_trim_color_token(value);
    const bool rgb = renderer2d_starts_with_i(value, "rgb(");
    const bool rgba = renderer2d_starts_with_i(value, "rgba(");
    if (!rgb && !rgba)
    {
        return std::nullopt;
    }

    const std::size_t open = value.find('(');
    if (open == std::string_view::npos || value.empty() || value.back() != ')')
    {
        return std::nullopt;
    }

    const std::string_view inner = renderer2d_trim_color_token(
        value.substr(open + 1u, value.size() - open - 2u));
    std::vector<Renderer2DColorNumber> components;
    components.reserve(4u);

    if (inner.find(',') != std::string_view::npos)
    {
        std::size_t start = 0u;
        while (start <= inner.size())
        {
            const std::size_t comma = inner.find(',', start);
            const std::size_t end = comma == std::string_view::npos ? inner.size() : comma;
            renderer2d_append_rgba_component(components, inner.substr(start, end - start));
            if (comma == std::string_view::npos)
            {
                break;
            }
            start = comma + 1u;
        }
    }
    else
    {
        std::size_t start = 0u;
        while (start < inner.size())
        {
            while (start < inner.size() && (renderer2d_color_space(inner[start]) || inner[start] == '/'))
            {
                ++start;
            }
            const std::size_t end = inner.find_first_of(" \t\n\r/", start);
            renderer2d_append_rgba_component(
                components,
                inner.substr(start, end == std::string_view::npos ? inner.size() - start : end - start));
            if (end == std::string_view::npos)
            {
                break;
            }
            start = end + 1u;
        }
    }

    if (components.size() != 3u && components.size() != 4u)
    {
        return std::nullopt;
    }

    const bool byteRgb =
        (!components[0].percent && components[0].value > 1.0f) ||
        (!components[1].percent && components[1].value > 1.0f) ||
        (!components[2].percent && components[2].value > 1.0f);
    auto rgb_component = [byteRgb](const Renderer2DColorNumber& component) {
        const float value = component.percent || !byteRgb ? component.value : component.value / 255.0f;
        return std::clamp(value, 0.0f, 1.0f);
    };
    auto alpha_component = [](const Renderer2DColorNumber& component) {
        const float value = (!component.percent && component.value > 1.0f) ?
            component.value / 255.0f :
            component.value;
        return std::clamp(value, 0.0f, 1.0f);
    };

    return glm::vec4 {
        rgb_component(components[0]),
        rgb_component(components[1]),
        rgb_component(components[2]),
        components.size() == 4u ? alpha_component(components[3]) : 1.0f
    };
}

inline std::optional<glm::vec4> renderer2d_parse_hex_color(std::string_view value)
{
    value = renderer2d_trim_color_token(value);
    if (std::optional<glm::vec4> rgba = renderer2d_parse_rgba_function_color(value))
    {
        return rgba;
    }

    if (!value.empty() && value.front() == '#')
    {
        value.remove_prefix(1u);
    }
    else if (value.size() > 2u && value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
    {
        value.remove_prefix(2u);
    }

    uint32_t red = 0u;
    uint32_t green = 0u;
    uint32_t blue = 0u;
    uint32_t alpha = 255u;

    if (value.size() == 3u || value.size() == 4u)
    {
        const std::optional<uint32_t> r = renderer2d_hex_digit(value[0]);
        const std::optional<uint32_t> g = renderer2d_hex_digit(value[1]);
        const std::optional<uint32_t> b = renderer2d_hex_digit(value[2]);
        if (!r || !g || !b)
        {
            return std::nullopt;
        }
        red = (*r << 4u) | *r;
        green = (*g << 4u) | *g;
        blue = (*b << 4u) | *b;
        if (value.size() == 4u)
        {
            const std::optional<uint32_t> a = renderer2d_hex_digit(value[3]);
            if (!a)
            {
                return std::nullopt;
            }
            alpha = (*a << 4u) | *a;
        }
    }
    else if (value.size() == 6u || value.size() == 8u)
    {
        const std::optional<uint32_t> r = renderer2d_hex_byte(value.substr(0u, 2u));
        const std::optional<uint32_t> g = renderer2d_hex_byte(value.substr(2u, 2u));
        const std::optional<uint32_t> b = renderer2d_hex_byte(value.substr(4u, 2u));
        if (!r || !g || !b)
        {
            return std::nullopt;
        }
        red = *r;
        green = *g;
        blue = *b;
        if (value.size() == 8u)
        {
            const std::optional<uint32_t> a = renderer2d_hex_byte(value.substr(6u, 2u));
            if (!a)
            {
                return std::nullopt;
            }
            alpha = *a;
        }
    }
    else
    {
        return std::nullopt;
    }

    constexpr float inv255 = 1.0f / 255.0f;
    return glm::vec4 {
        static_cast<float>(red) * inv255,
        static_cast<float>(green) * inv255,
        static_cast<float>(blue) * inv255,
        static_cast<float>(alpha) * inv255
    };
}

inline std::optional<glm::vec4> renderer2d_parse_color(std::string_view value)
{
    return renderer2d_parse_hex_color(value);
}

inline glm::vec4 renderer2d_hex_color(std::string_view value, glm::vec4 fallback = glm::vec4(1.0f))
{
    return renderer2d_parse_hex_color(value).value_or(fallback);
}

inline glm::vec4 renderer2d_color(std::string_view value, glm::vec4 fallback = glm::vec4(1.0f))
{
    return renderer2d_hex_color(value, fallback);
}

struct Transform2DComponent
{
    // Framebuffer-space transform applied after layout has resolved
    glm::vec2 position { 0.0f };
    glm::vec2 scale { 1.0f };
    glm::vec2 origin { 0.0f };
    float rotationRadians = 0.0f;
    glm::vec2 rotation3DRadians { 0.0f };
    float perspective = 0.0f;

    void set_2_5d(glm::vec2 tiltRadians, float perspectiveStrength = 0.75f)
    {
        rotation3DRadians = tiltRadians;
        perspective = std::max(perspectiveStrength, 0.0f);
    }
};

struct RenderLayer2DComponent
{
    int32_t layer = 0;
    uint32_t order = 0;
    bool visible = true;
    bool alwaysOnTop = false;
};

struct Parent2DComponent
{
    entt::entity parent = entt::null;
};

struct LayoutRect2DComponent
{
    glm::vec4 padding { 0.0f };
    bool resizeChildren = true;
    glm::vec2 childLayoutSize { 0.0f };
};

struct Mask2DComponent
{
    bool enabled = true;
    bool useContentRect = true;
    float effectPadding = 0.0f;
};

struct Layout2DComponent
{
    // Anchor and pivot layout mirrors common UI systems while staying renderer-native
    glm::vec2 anchorMin { 0.0f };
    glm::vec2 anchorMax { 0.0f };
    glm::vec2 pivot { 0.0f };
    glm::vec2 offset { 0.0f };
    glm::vec2 size { 0.0f };
    glm::vec4 margin { 0.0f };
};

enum class GridTrackUnit2D : uint32_t
{
    eFraction = 0,
    ePixels = 1
};

struct GridTrack2D
{
    GridTrackUnit2D unit = GridTrackUnit2D::eFraction;
    float value = 1.0f;

    static GridTrack2D fraction(float fraction)
    {
        return { GridTrackUnit2D::eFraction, fraction };
    }

    static GridTrack2D pixels(float pixels)
    {
        return { GridTrackUnit2D::ePixels, pixels };
    }
};

struct Grid2DComponent
{
    // Grid tracks can mix fixed pixels and fractions for menu-style panels
    std::vector<GridTrack2D> columns;
    std::vector<GridTrack2D> rows;
    glm::vec2 gap { 0.0f };
};

struct GridCell2DComponent
{
    uint32_t column = 0u;
    uint32_t row = 0u;
    uint32_t columnSpan = 1u;
    uint32_t rowSpan = 1u;
    glm::vec4 margin { 0.0f };
};

struct ShapeComponent
{
    // Geometry knobs used by the shape shader, including squircle, notch, and arc variants
    Renderer2DPrimitive primitive = Renderer2DPrimitive::eRoundedRectangle;
    glm::vec2 size { 100.0f, 40.0f };
    float cornerRadius = 0.0f;
    glm::vec4 cornerRadii { 0.0f };
    float squircleAmount = 1.0f;
    float squirclePower = 4.0f;
    float notchAmount = 0.0f;
    float notchDepth = 0.0f;
    float arcProgress = 1.0f;
    float arcThickness = 4.0f;
    float arcStartAngleRadians = -1.57079632679f;
    bool arcClockwise = true;
    bool customCornerRadii = false;
    bool sdfEdges = true;
    bool draggable = false;

    void set_corner_radius(float radius)
    {
        cornerRadius = std::max(radius, 0.0f);
        cornerRadii = glm::vec4(cornerRadius);
        customCornerRadii = false;
    }

    void set_corner_radii(float topLeft, float topRight, float bottomRight, float bottomLeft)
    {
        cornerRadii = glm::max(
            glm::vec4(topLeft, topRight, bottomRight, bottomLeft),
            glm::vec4(0.0f));
        cornerRadius = std::max(std::max(cornerRadii.x, cornerRadii.y), std::max(cornerRadii.z, cornerRadii.w));
        customCornerRadii =
            std::abs(cornerRadii.x - cornerRadii.y) > 0.001f ||
            std::abs(cornerRadii.x - cornerRadii.z) > 0.001f ||
            std::abs(cornerRadii.x - cornerRadii.w) > 0.001f;
    }

    glm::vec4 effective_corner_radii() const
    {
        return customCornerRadii ? cornerRadii : glm::vec4(std::max(cornerRadius, 0.0f));
    }
};

struct SystemBackdropComponent
{
    // Bounds and shape are resolved from the entity every frame. This value
    // only describes the requested eSystemGlass provider recipe.
    SystemBackdropRegion region {};
};

struct LiquidGlassComponent
{
    // Renderer-owned liquid glass request. It is intentionally separate from
    // SystemBackdropComponent so eLiquid can never enter a native compositor
    // effect graph. The entity supplies bounds and shape when a backend is
    // implemented.
    GlassMaterial material = GlassMaterial::eOff;
    LiquidGlassBackend backend = LiquidGlassBackend::eEngineRenderer;
};

struct DragHandle2DComponent
{
    entt::entity target = entt::null;
    bool enabled = true;
};

struct ShapeStyleComponent
{
    // Paint information for shape, gradient, outline, opacity, and backdrop blur
    Renderer2DFill fill = Renderer2DFill::eSolid;
    glm::vec4 color0 { 1.0f };
    glm::vec4 color1 { 1.0f };
    glm::vec4 outlineColor { 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec2 gradientStart { 0.0f, 0.0f };
    glm::vec2 gradientEnd { 1.0f, 0.0f };
    float outlineWidth = 0.0f;
    float edgeSoftness = 1.0f;
    float opacity = 1.0f;
    float backdropBlurRadius = 0.0f;
    uint32_t backdropBlurPasses = 1;
    float backdropBlurOpacity = 0.85f;
    bool backdropBlurFollowsFillAlpha = false;
    bool backdropBlurClipToInheritedMask = false;

    void set_color(glm::vec4 color)
    {
        fill = Renderer2DFill::eSolid;
        color0 = color;
        color1 = color;
    }

    bool set_color(std::string_view hexColor)
    {
        const std::optional<glm::vec4> color = renderer2d_parse_hex_color(hexColor);
        if (!color)
        {
            return false;
        }
        set_color(*color);
        return true;
    }

    void set_gradient(glm::vec4 startColor, glm::vec4 endColor, glm::vec2 start, glm::vec2 end)
    {
        fill = Renderer2DFill::eLinearGradient;
        color0 = startColor;
        color1 = endColor;
        gradientStart = start;
        gradientEnd = end;
    }

    void set_radial_gradient(glm::vec4 centerColor, glm::vec4 edgeColor, glm::vec2 center, float radius)
    {
        fill = Renderer2DFill::eRadialGradient;
        color0 = centerColor;
        color1 = edgeColor;
        gradientStart = center;
        gradientEnd = center + glm::vec2(std::max(radius, 0.0001f), 0.0f);
    }

    bool set_gradient(std::string_view startColor, std::string_view endColor, glm::vec2 start, glm::vec2 end)
    {
        const std::optional<glm::vec4> startParsed = renderer2d_parse_hex_color(startColor);
        const std::optional<glm::vec4> endParsed = renderer2d_parse_hex_color(endColor);
        if (!startParsed || !endParsed)
        {
            return false;
        }
        set_gradient(*startParsed, *endParsed, start, end);
        return true;
    }

    bool set_outline_color(std::string_view hexColor)
    {
        const std::optional<glm::vec4> color = renderer2d_parse_hex_color(hexColor);
        if (!color)
        {
            return false;
        }
        outlineColor = *color;
        return true;
    }

    void set_opacity_gradient(glm::vec2 start, glm::vec2 end, float startOpacity = 1.0f, float endOpacity = 0.0f)
    {
        fill = Renderer2DFill::eLinearGradient;
        color1 = color0;
        color0.a = std::clamp(startOpacity, 0.0f, 1.0f);
        color1.a = std::clamp(endOpacity, 0.0f, 1.0f);
        gradientStart = start;
        gradientEnd = end;
    }

    void set_backdrop_blur(float radius, uint32_t passes = 1, float blurOpacity = 0.85f)
    {
        backdropBlurRadius = std::max(radius, 0.0f);
        backdropBlurPasses = std::max(passes, 1u);
        backdropBlurOpacity = std::clamp(blurOpacity, 0.0f, 1.0f);
    }
};

struct ShadowComponent
{
    glm::vec4 color { 0.0f, 0.0f, 0.0f, 0.35f };
    glm::vec2 offset { 0.0f, 8.0f };
    float blurRadius = 16.0f;
    float spread = 0.0f;
    float opacity = 1.0f;
    // Removes shadow coverage beneath the source primitive. This is useful for
    // translucent glass because the shadow cannot darken the material itself.
    bool outsideOnly = false;

    bool set_color(std::string_view hexColor)
    {
        const std::optional<glm::vec4> parsed = renderer2d_parse_hex_color(hexColor);
        if (!parsed)
        {
            return false;
        }
        color = *parsed;
        return true;
    }
};

struct BlurComponent
{
    float radius = 8.0f;
    uint32_t passes = 1;
    float opacity = 0.85f;
};

struct PanelBlurComponent
{
    glm::vec2 offset { 0.0f };
    float radius = 16.0f;
    uint32_t passes = 1;
    float spread = 0.0f;
    float featherRadius = 32.0f;
    float opacity = 1.0f;
};

struct Model3DHandle
{
    uint32_t id = 0;
    uint32_t firstTriangle = 0;
    uint32_t triangleCount = 0;

    bool valid() const
    {
        return id != 0 && triangleCount > 0;
    }

    explicit operator bool() const
    {
        return valid();
    }
};

struct Media2DHandle
{
    uint32_t id = 0;
    Media2DSourceType sourceType = Media2DSourceType::eUnknown;
    glm::uvec2 pixelSize { 0u };
    double durationSeconds = 0.0;
    double frameRate = 0.0;
    uint32_t frameCount = 1;
    std::vector<double> frameDurationsSeconds;
    bool animated = false;
    bool drawable = false;
    bool hasBlackBackground = false;
    bool premultipliedAlpha = false;

    bool valid() const
    {
        return id != 0;
    }

    explicit operator bool() const
    {
        return valid();
    }
};

struct Model3DComponent
{
    uint32_t modelId = 0;
    uint32_t firstTriangle = 0;
    uint32_t triangleCount = 0;
    glm::vec2 size { 0.0f };
    glm::vec3 position { 0.0f };
    glm::vec3 rotationRadians { 0.0f };
    glm::vec3 scale { 1.0f };
    glm::vec3 cameraPosition { 0.0f, 0.0f, 3.0f };
    glm::vec3 cameraTarget { 0.0f };
    glm::vec3 lightDirection { -0.35f, 0.65f, 0.68f };
    glm::vec4 materialColor { 1.0f };
    float fieldOfViewRadians = 0.78539816339f;
    float nearPlane = 0.01f;
    float farPlane = 100.0f;
    bool visible = true;
    bool clipToBounds = true;
    bool clipToParent = true;

    void set_model(Model3DHandle handle)
    {
        modelId = handle.id;
        firstTriangle = handle.firstTriangle;
        triangleCount = handle.triangleCount;
        visible = handle.valid();
    }

    bool set_material_color(std::string_view hexColor)
    {
        const std::optional<glm::vec4> parsed = renderer2d_parse_hex_color(hexColor);
        if (!parsed)
        {
            return false;
        }
        materialColor = *parsed;
        return true;
    }
};

struct Media2DComponent
{
    // Media components draw uploaded raster, SVG, GIF, APNG, or video frames
    uint32_t mediaId = 0;
    Media2DSourceType sourceType = Media2DSourceType::eUnknown;
    glm::uvec2 sourcePixelSize { 0u };
    glm::vec2 size { 0.0f };
    Renderer2DFill tintFill = Renderer2DFill::eSolid;
    glm::vec4 tint { 1.0f };
    glm::vec4 tintEnd { 1.0f };
    glm::vec2 gradientStart { 0.0f, 0.0f };
    glm::vec2 gradientEnd { 1.0f, 0.0f };
    glm::vec4 uvRect { 0.0f, 0.0f, 1.0f, 1.0f };
    Media2DFit fit = Media2DFit::eStretch;
    float opacity = 1.0f;
    float cornerRadius = 0.0f;
    float edgeSoftness = 1.0f;
    float blurRadius = 0.0f;
    float brightness = 0.0f;
    float contrast = 1.0f;
    float exposure = 0.0f;
    float invert = 0.0f;
    bool autoLiftBlack = false;
    bool sourceHasBlackBackground = false;
    bool premultipliedAlpha = false;
    double durationSeconds = 0.0;
    double frameRate = 0.0;
    double playbackSeconds = 0.0;
    double lastPlaybackUpdateSeconds = -1.0;
    uint32_t currentFrame = 0;
    uint32_t frameCount = 1;
    std::vector<double> frameDurationsSeconds;
    bool animated = false;
    bool playing = true;
    bool loop = true;
    bool visible = true;
    bool drawable = false;
    bool tintAsMask = false;

    void set_media(Media2DHandle handle)
    {
        mediaId = handle.id;
        sourceType = handle.sourceType;
        sourcePixelSize = handle.pixelSize;
        durationSeconds = handle.durationSeconds;
        frameRate = handle.frameRate;
        frameCount = std::max(handle.frameCount, 1u);
        frameDurationsSeconds = handle.frameDurationsSeconds;
        animated = handle.animated;
        drawable = handle.drawable;
        sourceHasBlackBackground = handle.hasBlackBackground;
        premultipliedAlpha = handle.premultipliedAlpha;
    }

    void set_tint(glm::vec4 color)
    {
        tintFill = Renderer2DFill::eSolid;
        tint = color;
        tintEnd = color;
    }

    bool set_tint(std::string_view hexColor)
    {
        const std::optional<glm::vec4> color = renderer2d_parse_hex_color(hexColor);
        if (!color)
        {
            return false;
        }
        set_tint(*color);
        return true;
    }

    void set_tint_as_mask(bool enabled = true)
    {
        tintAsMask = enabled;
    }

    bool set_mask_tint(std::string_view color)
    {
        if (!set_tint(color))
        {
            return false;
        }
        set_tint_as_mask(true);
        return true;
    }

    void set_rgb_multiplier(glm::vec3 multiplier)
    {
        tint.r = multiplier.r;
        tint.g = multiplier.g;
        tint.b = multiplier.b;
        tintEnd.r = multiplier.r;
        tintEnd.g = multiplier.g;
        tintEnd.b = multiplier.b;
    }

    bool set_rgb_multiplier(std::string_view hexColor)
    {
        const std::optional<glm::vec4> color = renderer2d_parse_hex_color(hexColor);
        if (!color)
        {
            return false;
        }
        set_rgb_multiplier(glm::vec3(*color));
        return true;
    }

    void set_tint_gradient(glm::vec4 startTint, glm::vec4 endTint, glm::vec2 start, glm::vec2 end)
    {
        tintFill = Renderer2DFill::eLinearGradient;
        tint = startTint;
        tintEnd = endTint;
        gradientStart = start;
        gradientEnd = end;
    }

    bool set_tint_gradient(std::string_view startTint, std::string_view endTint, glm::vec2 start, glm::vec2 end)
    {
        const std::optional<glm::vec4> startColor = renderer2d_parse_hex_color(startTint);
        const std::optional<glm::vec4> endColor = renderer2d_parse_hex_color(endTint);
        if (!startColor || !endColor)
        {
            return false;
        }
        set_tint_gradient(*startColor, *endColor, start, end);
        return true;
    }

    void set_opacity_gradient(glm::vec2 start, glm::vec2 end, float startOpacity = 1.0f, float endOpacity = 0.0f)
    {
        tintFill = Renderer2DFill::eLinearGradient;
        tintEnd = tint;
        tint.a = std::clamp(startOpacity, 0.0f, 1.0f);
        tintEnd.a = std::clamp(endOpacity, 0.0f, 1.0f);
        gradientStart = start;
        gradientEnd = end;
    }

    void set_color_adjustment(
        float brightnessValue = 0.0f,
        float contrastValue = 1.0f,
        float exposureStops = 0.0f,
        float invertAmount = 0.0f)
    {
        brightness = std::clamp(brightnessValue, -1.0f, 1.0f);
        contrast = std::clamp(contrastValue, 0.0f, 4.0f);
        exposure = std::clamp(exposureStops, -4.0f, 4.0f);
        invert = std::clamp(invertAmount, 0.0f, 1.0f);
    }

    void set_auto_black_lift(bool enabled = true)
    {
        autoLiftBlack = enabled;
    }
};

struct Renderer2DCacheComponent
{
    // Static UI can be cached while dynamic subtrees opt out during animation or scroll
    Renderer2DCacheMode mode = Renderer2DCacheMode::eStatic;
    float idleTickRate = 0.0f;
    float activeTickRate = 60.0f;
    float pendingActiveSeconds = 0.0f;
    double activeUntilSeconds = 0.0;
    double lastDynamicSeconds = -1.0;
    bool wasActive = false;
    bool propagateToChildren = true;
    bool restoreStaticWhenIdle = false;
    bool detachedFromStaticLayer = false;
};

struct ScrollEdgeFade2DComponent
{
    // Applied to a scrolling content root. Descendants fade and foreground-
    // blur as their owning row approaches either edge of the masked viewport.
    bool enabled = true;
    float topHeight = 0.0f;
    float bottomHeight = 0.0f;
    float minimumOpacity = 0.0f;
    float maximumBlurRadius = 0.0f;
};

struct DisplayTransition2DComponent
{
    // Transition data can fade, blur, scale, repeat, spring, and destroy on completion
    bool enabled = true;
    bool inheritToChildren = true;
    bool removeWhenComplete = true;
    bool destroyEntityTreeOnComplete = false;
    bool hasStarted = false;
    bool delayScheduled = false;
    bool hasBaseScale = false;
    entt::entity destroyTarget = entt::null;
    double startSeconds = 0.0;
    float delaySeconds = 0.0f;
    float durationSeconds = 0.24f;
    float fromOpacity = 0.0f;
    float toOpacity = 1.0f;
    float fromBlurRadius = 0.0f;
    float toBlurRadius = 0.0f;
    glm::vec2 fromScale { 1.0f };
    glm::vec2 toScale { 1.0f };
    glm::vec2 baseScale { 1.0f };
    float speed = 1.0f;
    uint32_t repeatCount = 1u;
    bool autoreverses = true;
    glm::vec4 timingCurve { 0.3f, 1.0f, 0.9f, 0.7f };
    float springResponse = 0.38f;
    float springDampingFraction = 0.86f;
    float springBlendDuration = 0.0f;
    float springStiffness = 5.0f;
    float springDamping = 3.0f;
    DisplayTransitionCurve2D curve = DisplayTransitionCurve2D::eDefault;

    DisplayTransition2DComponent& set_delay(float seconds)
    {
        delaySeconds = std::max(seconds, 0.0f);
        return *this;
    }

    DisplayTransition2DComponent& set_speed(float value)
    {
        speed = std::max(value, 0.001f);
        return *this;
    }

    DisplayTransition2DComponent& set_repeat(uint32_t count, bool shouldAutoreverse = true)
    {
        repeatCount = std::max(count, 1u);
        autoreverses = shouldAutoreverse;
        return *this;
    }

    DisplayTransition2DComponent& set_timing_curve(float x1, float y1, float x2, float y2)
    {
        curve = DisplayTransitionCurve2D::eTimingCurve;
        timingCurve = { x1, y1, x2, y2 };
        return *this;
    }

    DisplayTransition2DComponent& set_spring(
        float response = 0.38f,
        float dampingFraction = 0.86f,
        float blendDuration = 0.0f)
    {
        curve = DisplayTransitionCurve2D::eSpring;
        springResponse = std::max(response, 0.001f);
        springDampingFraction = std::max(dampingFraction, 0.001f);
        springBlendDuration = std::max(blendDuration, 0.0f);
        return *this;
    }

    DisplayTransition2DComponent& set_interactive_spring(
        float response = 0.34f,
        float dampingFraction = 0.78f,
        float blendDuration = 0.0f)
    {
        curve = DisplayTransitionCurve2D::eInteractiveSpring;
        springResponse = std::max(response, 0.001f);
        springDampingFraction = std::max(dampingFraction, 0.001f);
        springBlendDuration = std::max(blendDuration, 0.0f);
        return *this;
    }

    DisplayTransition2DComponent& set_interpolating_spring(float stiffness = 5.0f, float damping = 3.0f)
    {
        curve = DisplayTransitionCurve2D::eInterpolatingSpring;
        springStiffness = std::max(stiffness, 0.001f);
        springDamping = std::max(damping, 0.0f);
        return *this;
    }
};

struct MSDFGlyph
{
    uint32_t codepoint = 0;
    glm::vec2 position { 0.0f };
    glm::vec2 size { 0.0f };
    glm::vec2 uvMin { 0.0f };
    glm::vec2 uvMax { 1.0f };
    float advance = 0.0f;
};

struct MSDFGlyphAtlasComponent
{
    uint32_t atlasId = 0;
    std::string name;
    glm::uvec2 textureSize { 0u };
    float pixelRange = 4.0f;
};

struct TextComponent
{
    std::string text;
    uint32_t atlasId = 0;
    float fontSize = 16.0f;
    float msdfPixelRange = 4.0f;
    glm::vec2 bounds { 0.0f };
    bool useMsdf = true;
    std::vector<MSDFGlyph> glyphs;
};

struct TextStyleComponent
{
    // Text style is separate from TextComponent so the same glyph layout can be restyled
    Renderer2DFill fill = Renderer2DFill::eSolid;
    glm::vec4 color0 { 1.0f };
    glm::vec4 color1 { 1.0f };
    glm::vec4 effectColor { 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec4 shadowColor { 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec2 gradientStart { 0.0f, 0.0f };
    glm::vec2 gradientEnd { 1.0f, 0.0f };
    glm::vec2 shadowOffset { 0.0f, 2.0f };
    float outlineWidth = 0.0f;
    float shadowBlur = 0.0f;
    float glowRadius = 0.0f;
    float blurRadius = 0.0f;
    float opacity = 1.0f;
    float fontWeight = 400.0f;
    float fontWeightExpansion = 0.0f;

    void set_color(glm::vec4 color)
    {
        fill = Renderer2DFill::eSolid;
        color0 = color;
        color1 = color;
    }

    bool set_color(std::string_view hexColor)
    {
        const std::optional<glm::vec4> color = renderer2d_parse_hex_color(hexColor);
        if (!color)
        {
            return false;
        }
        set_color(*color);
        return true;
    }

    void set_gradient(glm::vec4 startColor, glm::vec4 endColor, glm::vec2 start, glm::vec2 end)
    {
        fill = Renderer2DFill::eLinearGradient;
        color0 = startColor;
        color1 = endColor;
        gradientStart = start;
        gradientEnd = end;
    }

    bool set_gradient(std::string_view startColor, std::string_view endColor, glm::vec2 start, glm::vec2 end)
    {
        const std::optional<glm::vec4> startParsed = renderer2d_parse_hex_color(startColor);
        const std::optional<glm::vec4> endParsed = renderer2d_parse_hex_color(endColor);
        if (!startParsed || !endParsed)
        {
            return false;
        }
        set_gradient(*startParsed, *endParsed, start, end);
        return true;
    }

    bool set_effect_color(std::string_view hexColor)
    {
        const std::optional<glm::vec4> color = renderer2d_parse_hex_color(hexColor);
        if (!color)
        {
            return false;
        }
        effectColor = *color;
        return true;
    }

    bool set_shadow_color(std::string_view hexColor)
    {
        const std::optional<glm::vec4> color = renderer2d_parse_hex_color(hexColor);
        if (!color)
        {
            return false;
        }
        shadowColor = *color;
        return true;
    }

    void set_font_weight(float weight)
    {
        fontWeight = std::clamp(weight, 100.0f, 900.0f);
        fontWeightExpansion = std::clamp((fontWeight - 400.0f) / 500.0f * 0.85f, -0.75f, 0.95f);
    }

    void set_font_weight_expansion(float expansionPixels)
    {
        fontWeightExpansion = std::clamp(expansionPixels, -2.0f, 2.0f);
    }
};

struct Renderer2DBatch
{
    entt::entity entity = entt::null;
    int32_t stackLayer = 0;
    uint32_t stackOrder = 0;
    int32_t layer = 0;
    uint32_t order = 0;
    bool alwaysOnTop = false;
    Renderer2DPrimitive primitive = Renderer2DPrimitive::eRectangle;
    glm::vec4 rect { 0.0f };
    glm::vec4 clipRect { -1000000.0f, -1000000.0f, 2000000.0f, 2000000.0f };
    glm::vec4 uvRect { 0.0f };
    glm::vec4 color0 { 1.0f };
    glm::vec4 color1 { 1.0f };
    glm::vec4 color2 { 0.0f };
    glm::vec4 shadowColor { 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec4 effect0 { 0.0f };
    glm::vec4 effect1 { 0.0f };
    uint32_t flags = eRenderer2DStyleNone;
    uint32_t packedData = 0;
    uint32_t mediaId = 0;
    uint32_t frameIndex = 0;
};

struct Renderer3DModelBatch
{
    entt::entity entity = entt::null;
    uint32_t modelId = 0;
    uint32_t firstTriangle = 0;
    uint32_t triangleCount = 0;
    int32_t stackLayer = 0;
    uint32_t stackOrder = 0;
    int32_t layer = 0;
    uint32_t order = 0;
    bool alwaysOnTop = false;
    glm::vec4 viewportRect { 0.0f };
    glm::vec4 clipRect { 0.0f };
    glm::vec3 position { 0.0f };
    glm::vec3 rotationRadians { 0.0f };
    glm::vec3 scale { 1.0f };
    glm::vec3 cameraPosition { 0.0f, 0.0f, 3.0f };
    glm::vec3 cameraTarget { 0.0f };
    glm::vec3 lightDirection { -0.35f, 0.65f, 0.68f };
    glm::vec4 materialColor { 1.0f };
    float fieldOfViewRadians = 0.78539816339f;
    float nearPlane = 0.01f;
    float farPlane = 100.0f;
};

struct Renderer2DRenderPlan
{
    std::vector<Renderer2DBatch> panelBlurs;
    std::vector<Renderer2DBatch> shadows;
    std::vector<Renderer2DBatch> blurs;
    std::vector<Renderer2DBatch> shapes;
    std::vector<Renderer2DBatch> media;
    std::vector<Renderer2DBatch> texts;
    std::vector<Renderer3DModelBatch> models;
    std::vector<Renderer2DBatch> cachedPanelBlurs;
    std::vector<Renderer2DBatch> cachedShadows;
    std::vector<Renderer2DBatch> cachedBlurs;
    std::vector<Renderer2DBatch> cachedShapes;
    std::vector<Renderer2DBatch> cachedMedia;
    std::vector<Renderer2DBatch> cachedTexts;
    bool rebuildCachedLayer = false;
    bool usesHosted3D = false;

    void clear();
    void reserve(
        std::size_t shadowCount,
        std::size_t blurCount,
        std::size_t shapeCount,
        std::size_t mediaCount,
        std::size_t textCount,
        std::size_t modelCount = 0);
    bool empty() const;
};

class VIBRANCE_ENGINE_API Renderer2DScene
{
public:
    // ECS scene owns UI entities and builds sorted render plans for Renderer2D
    entt::registry& registry();
    const entt::registry& registry() const;

    entt::entity create_entity();
    entt::entity create_shape(
        glm::vec2 position,
        glm::vec2 size,
        const ShapeStyleComponent& style = {},
        Renderer2DPrimitive primitive = Renderer2DPrimitive::eRoundedRectangle,
        bool draggable = false
    );
    entt::entity create_text(
        std::string text,
        glm::vec2 position,
        float fontSize,
        const TextStyleComponent& style = {}
    );
    entt::entity create_model(
        glm::vec2 position,
        glm::vec2 size,
        const Model3DComponent& model = {}
    );
    entt::entity create_media(
        glm::vec2 position,
        glm::vec2 size,
        Media2DHandle media,
        Media2DFit fit = Media2DFit::eStretch
    );
    bool enable_mask(entt::entity entity, bool useContentRect = true, float effectPadding = 0.0f);
    bool disable_mask(entt::entity entity);

    void destroy_entity(entt::entity entity);
    bool destroy_entity_tree(entt::entity entity);
    void clear();
    void mark_dirty();
    void mark_dirty(entt::entity entity);
    void activate_dynamic(entt::entity entity, float durationSeconds);
    bool play_display_transition(
        entt::entity entity,
        const DisplayTransition2DComponent& transition,
        double currentTimeSeconds);
    bool clear_display_transition(entt::entity entity);
    uint64_t cache_generation() const;
    // Changes for any rendered entity, including dynamic entities that do not
    // invalidate the reusable static cache.
    uint64_t frame_generation() const;
    // True when time alone can change visible output (transitions, animated
    // media, or a timed cache refresh). Ordinary component changes are tracked
    // by frame_generation().
    bool requires_continuous_redraw(double currentTimeSeconds) const;
    bool hit_test(glm::vec2 point);
    entt::entity entity_at(glm::vec2 point);
    entt::entity draggable_parent_at(glm::vec2 point);
    bool draggable_hit_test(glm::vec2 point);

    Renderer2DRenderPlan build_render_plan(double currentTimeSeconds, uint64_t rendererCacheGeneration);
    void build_render_plan(Renderer2DRenderPlan& plan, double currentTimeSeconds, uint64_t rendererCacheGeneration);

private:
    friend class Renderer2D;
    bool damage_pending(entt::entity entity, double currentTimeSeconds) const;
    void commit_presented_bounds(
        std::vector<std::pair<entt::entity, glm::uvec4>> bounds);

    entt::registry registry_;
    uint64_t cacheGeneration_ = 1;
    uint64_t frameGeneration_ = 1;
    bool fullDamagePending_ = true;
    std::vector<entt::entity> damageEntities_;
    std::vector<std::pair<entt::entity, glm::uvec4>> presentedBounds_;
};
