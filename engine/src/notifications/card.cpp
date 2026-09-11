#include <vibranceUI/notifications/card.h>

#include <vibranceUI/localisation/localisation.h>
#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/ui/builder.h>
#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/glass.h>
#include <vibranceUI/ui/styles.h>
#include <vibranceUI/ui/surfaces.h>
#include <vibranceUI/ui/visuals.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    TextStyleComponent notification_text_style(
        std::string_view color,
        float weight)
    {
        TextStyleComponent style = make_flat_text_style(color);
        style.set_font_weight(weight);
        return style;
    }

    std::string supported_text(
        const Renderer2DFontAtlas& fontAtlas,
        const std::string& value,
        float fontSize)
    {
        std::string filtered;
        filtered.reserve(value.size());
        std::size_t offset = 0u;
        while (offset < value.size())
        {
            if (value[offset] == '?' && offset + 1u < value.size() &&
                value[offset + 1u] == '?')
            {
                do
                {
                    ++offset;
                }
                while (offset < value.size() && value[offset] == '?');
                continue;
            }
            const std::size_t next = ui_next_utf8_boundary(value, offset);
            if (next <= offset)
            {
                break;
            }
            const std::string_view codepoint = std::string_view(value).substr(
                offset,
                next - offset);
            const unsigned char first = static_cast<unsigned char>(
                codepoint.front());
            bool renderedAsQuestionMark = false;
            if (first >= 0x80u)
            {
                const Renderer2DTextLayout glyph =
                    fontAtlas.layout_text(codepoint, fontSize);
                renderedAsQuestionMark = glyph.glyphs.size() == 1u &&
                    glyph.glyphs.front().codepoint ==
                        static_cast<std::uint32_t>('?');
            }
            if (!renderedAsQuestionMark)
            {
                filtered.append(codepoint);
            }
            offset = next;
        }
        return filtered;
    }

    bool wrap_space(char value)
    {
        return value == ' ' || value == '\t' || value == '\r';
    }

    void skip_wrap_separators(
        const std::string& value,
        std::size_t& offset)
    {
        while (offset < value.size() &&
            (wrap_space(value[offset]) || value[offset] == '\n'))
        {
            ++offset;
        }
    }

    bool text_remains(const std::string& value, std::size_t offset)
    {
        skip_wrap_separators(value, offset);
        return offset < value.size();
    }

    void trim_line(std::string& line)
    {
        while (!line.empty() && wrap_space(line.back()))
        {
            line.pop_back();
        }
    }

    std::string truncate_to_width(
        const Renderer2DFontAtlas& fontAtlas,
        std::string value,
        float fontSize,
        float maximumWidth,
        bool forceEllipsis = false)
    {
        if (maximumWidth <= 0.0f)
        {
            return {};
        }
        constexpr std::string_view ellipsis = "...";
        if (!forceEllipsis &&
            ui_text_width(fontAtlas, value, fontSize) <= maximumWidth)
        {
            return value;
        }
        if (ui_text_width(fontAtlas, ellipsis, fontSize) > maximumWidth)
        {
            return {};
        }

        trim_line(value);
        while (!value.empty())
        {
            const std::string candidate = value + std::string(ellipsis);
            if (ui_text_width(fontAtlas, candidate, fontSize) <= maximumWidth)
            {
                return candidate;
            }
            value.resize(ui_previous_utf8_boundary(value, value.size()));
            trim_line(value);
        }
        return std::string(ellipsis);
    }

    std::array<std::string, 3u> wrap_to_width(
        const Renderer2DFontAtlas& fontAtlas,
        std::string_view message,
        float fontSize,
        float maximumLineWidth)
    {
        const std::string value(message);
        std::array<std::string, 3u> lines {};
        if (value.empty() || maximumLineWidth <= 0.0f)
        {
            return lines;
        }

        std::size_t cursor = 0u;
        for (std::size_t lineIndex = 0u;
            lineIndex < lines.size() && cursor < value.size();
            ++lineIndex)
        {
            skip_wrap_separators(value, cursor);
            if (cursor >= value.size())
            {
                break;
            }

            const std::size_t lineStart = cursor;
            std::size_t fittingEnd = cursor;
            std::size_t scan = cursor;
            std::size_t lastSpace = std::string::npos;
            bool forcedBreak = false;
            while (scan < value.size())
            {
                if (value[scan] == '\n')
                {
                    forcedBreak = true;
                    break;
                }
                if (wrap_space(value[scan]))
                {
                    lastSpace = scan;
                }
                const std::size_t next = ui_next_utf8_boundary(value, scan);
                if (next <= scan)
                {
                    break;
                }
                const std::string_view candidate(
                    value.data() + lineStart,
                    next - lineStart);
                if (ui_text_width(fontAtlas, candidate, fontSize) >
                    maximumLineWidth)
                {
                    break;
                }
                fittingEnd = next;
                scan = next;
            }

            const bool reachedEnd = scan >= value.size();
            if (forcedBreak)
            {
                cursor = scan + 1u;
            }
            else if (reachedEnd)
            {
                cursor = value.size();
            }
            else if (lastSpace != std::string::npos &&
                lastSpace > lineStart && lastSpace <= fittingEnd)
            {
                fittingEnd = lastSpace;
                cursor = lastSpace + 1u;
            }
            else if (fittingEnd > lineStart)
            {
                cursor = fittingEnd;
            }
            else
            {
                cursor = ui_next_utf8_boundary(value, lineStart);
            }

            lines[lineIndex] = value.substr(
                lineStart,
                fittingEnd - lineStart);
            trim_line(lines[lineIndex]);
        }

        if (text_remains(value, cursor))
        {
            lines.back() = truncate_to_width(
                fontAtlas,
                lines.back(),
                fontSize,
                maximumLineWidth,
                true);
        }
        return lines;
    }

    void ellipsize_by_characters(
        std::string& line,
        std::size_t maximumCharacters)
    {
        if (maximumCharacters == 0u)
        {
            line.clear();
            return;
        }
        if (maximumCharacters <= 3u)
        {
            line.assign(maximumCharacters, '.');
            return;
        }

        std::vector<std::size_t> boundaries { 0u };
        std::size_t offset = 0u;
        while (offset < line.size())
        {
            const std::size_t next = ui_next_utf8_boundary(line, offset);
            if (next <= offset)
            {
                break;
            }
            boundaries.push_back(next);
            offset = next;
        }
        const std::size_t visibleCharacters = maximumCharacters - 3u;
        if (boundaries.size() - 1u > visibleCharacters)
        {
            line.resize(boundaries[visibleCharacters]);
        }
        trim_line(line);
        line += "...";
    }

    std::array<std::string, 3u> wrap_by_characters(
        std::string_view message,
        std::size_t maximumLineCharacters)
    {
        const std::string value(message);
        std::array<std::string, 3u> lines {};
        if (value.empty() || maximumLineCharacters == 0u)
        {
            return lines;
        }

        std::size_t cursor = 0u;
        for (std::size_t lineIndex = 0u;
            lineIndex < lines.size() && cursor < value.size();
            ++lineIndex)
        {
            skip_wrap_separators(value, cursor);
            if (cursor >= value.size())
            {
                break;
            }

            const std::size_t lineStart = cursor;
            std::size_t fittingEnd = cursor;
            std::size_t scan = cursor;
            std::size_t lastSpace = std::string::npos;
            std::size_t characters = 0u;
            bool forcedBreak = false;
            while (scan < value.size() &&
                characters < maximumLineCharacters)
            {
                if (value[scan] == '\n')
                {
                    forcedBreak = true;
                    break;
                }
                if (wrap_space(value[scan]))
                {
                    lastSpace = scan;
                }
                const std::size_t next = ui_next_utf8_boundary(value, scan);
                if (next <= scan)
                {
                    break;
                }
                fittingEnd = next;
                scan = next;
                ++characters;
            }

            const bool reachedEnd = scan >= value.size();
            if (forcedBreak)
            {
                cursor = scan + 1u;
            }
            else if (reachedEnd)
            {
                cursor = value.size();
            }
            else if (lastSpace != std::string::npos &&
                lastSpace > lineStart && lastSpace <= fittingEnd)
            {
                fittingEnd = lastSpace;
                cursor = lastSpace + 1u;
            }
            else
            {
                cursor = fittingEnd;
            }

            lines[lineIndex] = value.substr(
                lineStart,
                fittingEnd - lineStart);
            trim_line(lines[lineIndex]);
        }

        if (text_remains(value, cursor))
        {
            ellipsize_by_characters(
                lines.back(),
                maximumLineCharacters);
        }
        return lines;
    }

    entt::entity create_icon(
        UiBuilder& ui,
        entt::entity card,
        const UiNotificationCardMetrics& metrics,
        Media2DHandle iconMedia,
        std::uint32_t order)
    {
        if (!iconMedia.valid())
        {
            return entt::null;
        }
        UiSurfaceBlockOptions tileOptions = {};
        tileOptions.style = make_frosted_panel_style(
            "rgba(255, 255, 255, 0.94)",
            "rgba(237, 242, 246, 0.86)",
            "rgba(255, 255, 255, 0.72)",
            0.8f,
            1.0f,
            10.0f,
            1u,
            0.82f);
        tileOptions.primitive = Renderer2DPrimitive::eSquircle;
        tileOptions.cornerRadius = 14.0f;
        tileOptions.layer = 4;
        tileOptions.order = order;
        const float iconTop =
            std::max((metrics.size.y - metrics.iconSize) * 0.5f, 12.0f);
        const entt::entity tile = ui_create_surface_block(
            ui,
            card,
            UiAlignment::eTopLeft,
            { 16.0f, iconTop },
            { metrics.iconSize, metrics.iconSize },
            tileOptions);

        const entt::entity media = ui.create_aligned_media(
            iconMedia,
            tile,
            UiAlignment::eCenter,
            { metrics.iconSize - 8.0f, metrics.iconSize - 8.0f },
            5,
            order,
            {},
            Media2DFit::eContain);
        if (Media2DComponent* component =
            ui.registry().try_get<Media2DComponent>(media))
        {
            component->primitive = Renderer2DPrimitive::eSquircle;
            component->cornerRadius = scaled_scalar(10.0f, ui.scale());
        }
        return tile;
    }
}

std::string ui_supported_notification_text(
    const Renderer2DFontAtlas& fontAtlas,
    const std::string& value,
    float fontSize)
{
    return supported_text(fontAtlas, value, fontSize);
}

std::array<std::string, 3u> ui_wrap_notification_message(
    std::string_view message,
    std::size_t maximumLineCharacters)
{
    return wrap_by_characters(message, maximumLineCharacters);
}

UiNotificationCardMetrics ui_measure_notification_card(
    const Renderer2DFontAtlas& fontAtlas,
    std::string_view message,
    bool hasOptions,
    bool hasIcon)
{
    UiNotificationCardMetrics metrics = {};
    const float contentLeft = hasIcon ? metrics.contentLeft : 16.0f;
    const float contentWidth = std::max(
        metrics.size.x - contentLeft - 14.0f,
        0.0f);
    const std::string supported = supported_text(
        fontAtlas,
        std::string(message),
        11.5f);
    const std::array<std::string, 3u> lines = wrap_to_width(
        fontAtlas,
        supported,
        11.5f,
        contentWidth);
    const bool usesThirdLine = !lines[2].empty();
    if (hasOptions)
    {
        metrics.size.y = usesThirdLine ?
            kUiNotificationThreeLineActionCardHeight :
            kUiNotificationActionCardHeight;
    }
    else
    {
        metrics.size.y = usesThirdLine ?
            kUiNotificationThreeLineCardHeight :
            kUiNotificationCardHeight;
    }
    return metrics;
}

UiNotificationCardHandle ui_create_notification_card(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    const Localisation& localisation,
    const UiNotificationCardOptions& options)
{
    UiNotificationCardHandle handle = {};
    handle.groupSize = std::max(options.groupSize, std::size_t(1u));
    const float layerOffset = std::max(options.layerOffset, 0.0f);
    const std::size_t visibleLayers = std::min(
        handle.groupSize - 1u,
        options.maximumLayers);
    const float layerDepth =
        static_cast<float>(visibleLayers) * layerOffset;

    handle.root = ui.scene().create_shape(
        { 0.0f, 0.0f },
        scaled_size(
            options.metrics.size.x,
            options.metrics.size.y + layerDepth,
            ui.scale()),
        make_solid_style(
            "rgba(0, 0, 0, 0)",
            "rgba(0, 0, 0, 0)",
            0.0f,
            0.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(handle.root, 0, options.order);
    ui.attach_aligned(
        handle.root,
        options.parent,
        UiAlignment::eTopLeft,
        scaled_offset(options.offset.x, options.offset.y, ui.scale()),
        scaled_size(
            options.metrics.size.x,
            options.metrics.size.y + layerDepth,
            ui.scale()));
    ui.registry().emplace<HitRegion2DComponent>(handle.root);
    ui.set_dynamic_cache(handle.root);

    for (std::size_t layerIndex = visibleLayers;
        layerIndex > 0u; --layerIndex)
    {
        const float inset = static_cast<float>(layerIndex) * 7.0f;
        const float verticalOffset =
            static_cast<float>(layerIndex) * layerOffset;
        UiSurfaceBlockOptions layerOptions = {};
        layerOptions.style = make_frosted_panel_style(
            layerIndex == 1u ?
                "rgba(222, 239, 243, 0.72)" :
                "rgba(218, 235, 240, 0.56)",
            layerIndex == 1u ?
                "rgba(194, 218, 225, 0.64)" :
                "rgba(190, 213, 220, 0.48)",
            "rgba(255, 255, 255, 0.54)",
            0.8f,
            1.0f,
            22.0f,
            1u,
            0.82f);
        layerOptions.primitive = Renderer2DPrimitive::eSquircle;
        layerOptions.cornerRadius = std::max(
            options.metrics.cornerRadius -
                static_cast<float>(layerIndex),
            12.0f);
        layerOptions.layer = 1;
        layerOptions.order = options.order + static_cast<std::uint32_t>(
            options.maximumLayers - layerIndex);
        ui_create_surface_block(
            ui,
            handle.root,
            UiAlignment::eTopLeft,
            { inset, verticalOffset },
            {
                options.metrics.size.x - inset * 2.0f,
                options.metrics.size.y
            },
            layerOptions);
    }

    UiSurfaceBlockOptions cardOptions = {};
    cardOptions.style = make_frosted_panel_style(
        "rgba(229, 243, 246, 0.76)",
        "rgba(204, 229, 235, 0.68)",
        "rgba(255, 255, 255, 0.62)",
        1.0f,
        1.0f,
        26.0f,
        2u,
        0.92f);
    cardOptions.primitive = Renderer2DPrimitive::eSquircle;
    cardOptions.cornerRadius = options.metrics.cornerRadius;
    cardOptions.layer = 2;
    cardOptions.order = options.order;
    handle.surface = ui_create_surface_block(
        ui,
        handle.root,
        UiAlignment::eTopLeft,
        { 0.0f, 0.0f },
        options.metrics.size,
        cardOptions);
    // The Vulkan surface remains responsible for the card contents. On
    // Windows, this region asks the D3D11 Composition bridge for the desktop
    // blur underneath the translucent pixels; elsewhere the renderer-owned
    // frosted style above remains the visual fallback.
    ui_apply_glass_material(
        ui,
        handle.surface,
        ui_system_glass_options(
            24.0f,
            1.12f,
            { 0.80f, 0.91f, 0.94f, 0.20f },
            SystemBackdropProvider::eEngine));

    ShadowComponent shadow = {};
    shadow.set_color("#07131E52");
    shadow.offset = scaled_offset(0.0f, 8.0f, ui.scale());
    shadow.blurRadius = scaled_scalar(16.0f, ui.scale());
    shadow.spread = scaled_scalar(1.0f, ui.scale());
    shadow.outsideOnly = true;
    ui.registry().emplace_or_replace<ShadowComponent>(handle.surface, shadow);

    if (options.callbacks.activate)
    {
        ButtonInputComponent cardInput = {};
        cardInput.onClick = [callback = options.callbacks.activate](
            const PointerInputEvent&) {
            callback();
        };
        ui.registry().emplace<ButtonInputComponent>(
            handle.root,
            std::move(cardInput));
    }

    handle.icon = create_icon(
        ui,
        handle.surface,
        options.metrics,
        options.iconMedia,
        options.order);
    const float contentLeft = handle.icon != entt::null ?
        options.metrics.contentLeft : 16.0f;
    const float contentWidth = std::max(
        options.metrics.size.x - contentLeft - 14.0f,
        0.0f);

    const std::string supportedHeader = supported_text(
        fontAtlas,
        options.content.header,
        10.5f);
    const bool headerIsRtl = fontAtlas.layout_text(
        supportedHeader,
        10.5f).rightToLeft;
    const std::string displayHeader = truncate_to_width(
        fontAtlas,
        supportedHeader,
        10.5f,
        std::max(contentWidth - 56.0f, 0.0f));
    ui.create_aligned_text(
        Text::literal(displayHeader),
        localisation,
        fontAtlas,
        handle.surface,
        headerIsRtl ? UiAlignment::eTopRight : UiAlignment::eTopLeft,
        10.5f,
        notification_text_style("#59737EFF", 520.0f),
        4,
        options.order,
        { headerIsRtl ? -70.0f : contentLeft, 10.0f });

    handle.timestampText = ui.create_aligned_text(
        Text::literal(options.content.elapsedText),
        localisation,
        fontAtlas,
        handle.surface,
        UiAlignment::eTopRight,
        10.5f,
        notification_text_style("#647B84D8", 460.0f),
        4,
        options.order + 1u,
        { -12.0f, 10.0f });
    ui.set_dynamic_cache(handle.timestampText, false);

    const std::string supportedTitle = supported_text(
        fontAtlas,
        options.content.title,
        14.5f);
    const bool titleIsRtl = fontAtlas.layout_text(
        supportedTitle,
        14.5f).rightToLeft;
    const std::string displayTitle = truncate_to_width(
        fontAtlas,
        supportedTitle,
        14.5f,
        contentWidth);
    ui.create_aligned_text(
        Text::literal(displayTitle),
        localisation,
        fontAtlas,
        handle.surface,
        titleIsRtl ? UiAlignment::eTopRight : UiAlignment::eTopLeft,
        14.5f,
        notification_text_style("#111B22FF", 680.0f),
        4,
        options.order + 2u,
        { titleIsRtl ? -14.0f : contentLeft, 27.0f });

    const std::string displayMessage = supported_text(
        fontAtlas,
        options.content.message,
        11.5f);
    const std::array<std::string, 3u> lines = wrap_to_width(
        fontAtlas,
        displayMessage,
        11.5f,
        contentWidth);
    for (std::size_t lineIndex = 0u;
        lineIndex < lines.size();
        ++lineIndex)
    {
        if (lines[lineIndex].empty())
        {
            continue;
        }
        const bool lineIsRtl = fontAtlas.layout_text(
            lines[lineIndex],
            11.5f).rightToLeft;
        ui.create_aligned_text(
            Text::literal(lines[lineIndex]),
            localisation,
            fontAtlas,
            handle.surface,
            lineIsRtl ? UiAlignment::eTopRight : UiAlignment::eTopLeft,
            11.5f,
            notification_text_style("#1E2C34F2", 460.0f),
            4,
            options.order + 3u + static_cast<std::uint32_t>(lineIndex),
            {
                lineIsRtl ? -14.0f : contentLeft,
                49.0f + 16.0f * static_cast<float>(lineIndex)
            });
    }

    UiIconButtonOptions dismissOptions = {};
    dismissOptions.icon = options.dismissIcon;
    dismissOptions.cornerRadius = 12.0f;
    dismissOptions.iconSize = 12.0f;
    dismissOptions.iconTint = "#4E6570FF";
    dismissOptions.backgroundColor = "rgba(220, 237, 241, 0.94)";
    dismissOptions.hoveredBackgroundColor =
        "rgba(240, 249, 251, 0.98)";
    dismissOptions.pressedBackgroundColor =
        "rgba(196, 220, 226, 0.98)";
    dismissOptions.outlineColor = "rgba(255, 255, 255, 0.64)";
    dismissOptions.outlineWidth = 0.9f;
    if (options.callbacks.dismiss)
    {
        dismissOptions.onClick = [callback = options.callbacks.dismiss](
            const PointerInputEvent&) {
            callback();
        };
    }
    handle.dismissButton = ui_create_icon_button(
        ui,
        handle.surface,
        UiAlignment::eTopLeft,
        { -7.0f, -7.0f },
        { 24.0f, 24.0f },
        8,
        options.order,
        std::move(dismissOptions)).root;
    if (RenderLayer2DComponent* layer =
        ui.registry().try_get<RenderLayer2DComponent>(handle.dismissButton))
    {
        layer->visible = false;
    }
    if (ButtonInputComponent* input =
        ui.registry().try_get<ButtonInputComponent>(handle.dismissButton))
    {
        input->enabled = false;
    }

    if (options.showOptions)
    {
        UiButtonOptions buttonOptions = {};
        buttonOptions.cornerRadius = 13.0f;
        buttonOptions.fontSize = 11.5f;
        buttonOptions.textColor = "#17232AFF";
        buttonOptions.backgroundColor = "rgba(168, 199, 205, 0.46)";
        buttonOptions.hoveredBackgroundColor =
            "rgba(187, 216, 222, 0.70)";
        buttonOptions.pressedBackgroundColor =
            "rgba(145, 184, 192, 0.72)";
        buttonOptions.outlineColor = "rgba(255, 255, 255, 0.22)";
        buttonOptions.hoveredOutlineColor =
            "rgba(255, 255, 255, 0.48)";
        buttonOptions.outlineWidth = 0.7f;
        if (options.callbacks.showOptions)
        {
            buttonOptions.onClick = [callback = options.callbacks.showOptions](
                const PointerInputEvent&) {
                callback();
            };
        }
        handle.optionsButton = ui_create_aligned_text_button(
            ui,
            fontAtlas,
            handle.surface,
            UiAlignment::eBottomRight,
            { -12.0f, -8.0f },
            options.content.optionsLabel,
            { 100.0f, 26.0f },
            7,
            options.order + 1u,
            std::move(buttonOptions)).root;
    }
    return handle;
}

void ui_prepare_notification_card_fan(
    UiBuilder& ui,
    UiNotificationCardHandle& card,
    glm::vec2 startOffset,
    double currentTimeSeconds,
    float delaySeconds)
{
    if (card.root == entt::null ||
        !ui.registry().valid(card.root) ||
        glm::length(startOffset) <= 0.01f)
    {
        return;
    }
    card.fanStartOffset = startOffset;
    card.fanStartSeconds = currentTimeSeconds +
        static_cast<double>(std::max(delaySeconds, 0.0f));
    VisualTransform2DComponent* visual =
        ui.registry().try_get<VisualTransform2DComponent>(card.root);
    if (!visual)
    {
        visual = &ui.registry().emplace<VisualTransform2DComponent>(
            card.root);
    }
    visual->offset = startOffset;
    ui.scene().mark_dirty(card.root);
}
