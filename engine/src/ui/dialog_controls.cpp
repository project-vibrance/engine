#include <vibranceUI/ui/dialog_controls.h>

#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/glass.h>
#include <vibranceUI/ui/styles.h>
#include <vibranceUI/ui/surfaces.h>
#include <vibranceUI/ui/text.h>
#include <vibranceUI/ui/visuals.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
    entt::entity create_dialog_surface(
        UiBuilder& ui,
        entt::entity parent,
        UiAlignment alignment,
        glm::vec2 offset,
        glm::vec2 size,
        int32_t layer,
        uint32_t order,
        bool glass)
    {
        UiSurfaceBlockOptions surface = {};
        surface.style = glass ?
            make_frosted_panel_style(
                "rgba(242, 243, 245, 0.93)",
                "rgba(211, 214, 220, 0.88)",
                "rgba(255, 255, 255, 0.76)",
                0.9f,
                1.0f,
                24.0f,
                2u,
                0.96f) :
            make_solid_style(
                "rgba(250, 250, 251, 0.985)",
                "rgba(255, 255, 255, 0.84)",
                0.9f,
                1.0f);
        surface.primitive = Renderer2DPrimitive::eSquircle;
        surface.cornerRadius = 30.0f;
        surface.squircleAmount = 0.86f;
        surface.squirclePower = 4.15f;
        surface.layer = layer;
        surface.order = order;
        // This is a background surface; children use higher layers and must
        // remain visible above it.
        surface.alwaysOnTop = false;
        const entt::entity root = ui_create_surface_block(
            ui,
            parent,
            alignment,
            offset,
            size,
            surface);
        if (glass)
        {
            ui_apply_glass_material(
                ui,
                root,
                ui_system_glass_options(
                    24.0f,
                    1.04f,
                    { 0.96f, 0.97f, 1.0f, 0.045f }));
        }
        ShadowComponent shadow = {};
        shadow.set_color("rgba(0, 0, 0, 0.30)");
        shadow.offset = scaled_offset(0.0f, 8.0f, ui.scale());
        shadow.blurRadius = scaled_scalar(28.0f, ui.scale());
        shadow.spread = scaled_scalar(1.0f, ui.scale());
        shadow.opacity = 1.0f;
        ui.registry().emplace_or_replace<ShadowComponent>(root, shadow);
        return root;
    }

    UiButtonOptions secondary_button_options(std::function<void()> action)
    {
        UiButtonOptions options = {};
        options.cornerRadius = 28.0f;
        options.fontSize = 19.0f;
        options.textColor = "#202124FF";
        options.backgroundColor = "rgba(166, 170, 176, 0.42)";
        options.hoveredBackgroundColor = "rgba(151, 156, 164, 0.54)";
        options.pressedBackgroundColor = "rgba(136, 141, 149, 0.62)";
        options.outlineColor = "rgba(255, 255, 255, 0.12)";
        options.onClick = [action = std::move(action)](
            const PointerInputEvent&) {
            if (action)
            {
                action();
            }
        };
        return options;
    }

    UiButtonOptions primary_button_options(
        bool destructive,
        std::function<void()> action)
    {
        UiButtonOptions options = {};
        options.cornerRadius = 28.0f;
        options.fontSize = 19.0f;
        options.primary = true;
        options.textColor = "#FFFFFFFF";
        options.backgroundColor = destructive ?
            "#E5484DFF" : "#087CFAFF";
        options.hoveredBackgroundColor = destructive ?
            "#D63C42FF" : "#0874E8FF";
        options.pressedBackgroundColor = destructive ?
            "#C9343AFF" : "#0067D4FF";
        options.outlineColor = "rgba(255, 255, 255, 0.22)";
        options.onClick = [action = std::move(action)](
            const PointerInputEvent&) {
            if (action)
            {
                action();
            }
        };
        return options;
    }

    ShapeStyleComponent verification_slot_style(
        bool filled,
        bool active)
    {
        return make_solid_style(
            filled ? "rgba(245, 245, 246, 1)" :
                "rgba(238, 238, 240, 0.94)",
            active ? "#1677FFFF" : "rgba(0, 0, 0, 0)",
            active ? 1.7f : 0.0f,
            1.0f);
    }

    TextStyleComponent dialog_text_style(
        std::string_view color,
        float weight)
    {
        TextStyleComponent style = ui_macos26_text_style(color);
        style.fontWeight = weight;
        return style;
    }
}

UiConfirmationDialogHandle ui_create_confirmation_dialog(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    UiConfirmationDialogOptions options)
{
    UiConfirmationDialogHandle handle = {};
    const glm::vec2 size {
        std::max(options.size.x, 360.0f),
        std::max(options.size.y, 300.0f)
    };
    handle.root = create_dialog_surface(
        ui,
        parent,
        alignment,
        offset,
        size,
        options.layer,
        options.order,
        true);

    UiSurfaceBlockOptions iconSlotOptions = {};
    iconSlotOptions.style = ui_clear_surface_style();
    iconSlotOptions.cornerRadius = 16.0f;
    iconSlotOptions.layer = options.layer + 2;
    iconSlotOptions.order = options.order + 1u;
    handle.iconSlot = ui_create_surface_block(
        ui,
        handle.root,
        UiAlignment::eTopLeft,
        { 42.0f, 38.0f },
        { 108.0f, 108.0f },
        iconSlotOptions);

    ui.create_aligned_text(
        options.title,
        fontAtlas,
        handle.root,
        UiAlignment::eTopLeft,
        23.0f,
        dialog_text_style("#202124FF", 720.0f),
        options.layer + 2,
        options.order + 2u,
        { 42.0f, 188.0f });
    if (!options.message.empty())
    {
        ui.create_aligned_text(
            options.message,
            fontAtlas,
            handle.root,
            UiAlignment::eTopLeft,
            19.0f,
            dialog_text_style("rgba(31, 32, 35, 0.90)", 430.0f),
            options.layer + 2,
            options.order + 3u,
            { 42.0f, size.y - 145.0f });
    }

    constexpr float buttonGap = 16.0f;
    constexpr float horizontalInset = 32.0f;
    constexpr float buttonHeight = 56.0f;
    const float buttonWidth = std::max(
        (size.x - horizontalInset * 2.0f - buttonGap) * 0.5f,
        120.0f);
    const float buttonY = size.y - buttonHeight - 28.0f;
    UiControlHandle cancel = ui_create_aligned_text_button(
        ui,
        fontAtlas,
        handle.root,
        UiAlignment::eTopLeft,
        { horizontalInset, buttonY },
        options.cancelLabel,
        { buttonWidth, buttonHeight },
        options.layer + 3,
        options.order + 4u,
        secondary_button_options(std::move(options.onCancel)));
    handle.cancelButton = cancel.root;

    UiControlHandle confirm = ui_create_aligned_text_button(
        ui,
        fontAtlas,
        handle.root,
        UiAlignment::eTopLeft,
        { horizontalInset + buttonWidth + buttonGap, buttonY },
        options.confirmLabel,
        { buttonWidth, buttonHeight },
        options.layer + 3,
        options.order + 5u,
        primary_button_options(
            options.destructive,
            std::move(options.onConfirm)));
    handle.confirmButton = confirm.root;
    return handle;
}

std::string ui_sanitise_verification_code(
    std::string_view value,
    std::size_t digitCount)
{
    std::string result;
    if (digitCount == 0u)
    {
        return result;
    }
    result.reserve(std::min(value.size(), digitCount));
    for (const char character : value)
    {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (!std::isdigit(byte))
        {
            continue;
        }
        result.push_back(character);
        if (result.size() >= digitCount)
        {
            break;
        }
    }
    return result;
}

UiVerificationCodeHandle ui_create_verification_code_dialog(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    std::shared_ptr<UiVerificationCodeState> state,
    UiVerificationCodeOptions options)
{
    if (!state)
    {
        state = std::make_shared<UiVerificationCodeState>();
    }
    const std::size_t digitCount = std::clamp<std::size_t>(
        options.digitCount,
        4u,
        10u);
    state->value = ui_sanitise_verification_code(
        state->value,
        digitCount);

    UiVerificationCodeHandle handle = {};
    const glm::vec2 size {
        std::max(options.size.x, 400.0f),
        std::max(options.size.y, 320.0f)
    };
    handle.root = create_dialog_surface(
        ui,
        parent,
        alignment,
        offset,
        size,
        options.layer,
        options.order,
        false);

    ui.create_aligned_text(
        options.title,
        fontAtlas,
        handle.root,
        UiAlignment::eTopLeft,
        16.0f,
        dialog_text_style("#202124FF", 700.0f),
        options.layer + 2,
        options.order + 1u,
        { 48.0f, 54.0f });
    ui.create_aligned_text(
        options.message,
        fontAtlas,
        handle.root,
        UiAlignment::eTopLeft,
        14.0f,
        dialog_text_style("rgba(50, 51, 56, 0.70)", 420.0f),
        options.layer + 2,
        options.order + 2u,
        { 48.0f, 82.0f });

    constexpr float slotWidth = 44.0f;
    constexpr float slotHeight = 48.0f;
    constexpr float slotGap = 8.0f;
    const float codeWidth = static_cast<float>(digitCount) * slotWidth +
        static_cast<float>(digitCount - 1u) * slotGap;
    constexpr float codeTop = 150.0f;
    const float codeLeft = 48.0f;
    handle.digitSlots.reserve(digitCount);
    handle.digitLabels.reserve(digitCount);
    for (std::size_t index = 0u; index < digitCount; ++index)
    {
        const bool filled = index < state->value.size();
        const bool active = index == std::min(
            state->value.size(),
            digitCount - 1u);
        UiSurfaceBlockOptions slotOptions = {};
        slotOptions.style = verification_slot_style(filled, active);
        slotOptions.primitive = Renderer2DPrimitive::eRoundedRectangle;
        slotOptions.cornerRadius = 9.0f;
        slotOptions.layer = options.layer + 2;
        slotOptions.order = options.order + 10u +
            static_cast<uint32_t>(index) * 2u;
        entt::entity slot = ui_create_surface_block(
            ui,
            handle.root,
            UiAlignment::eTopLeft,
            {
                codeLeft + static_cast<float>(index) *
                    (slotWidth + slotGap),
                codeTop
            },
            { slotWidth, slotHeight },
            slotOptions);
        // Keep a real glyph allocation for later edits, but hide unused
        // labels. Empty text must never be used as a visual placeholder.
        const std::string digit = filled ?
            std::string(1u, state->value[index]) : "0";
        entt::entity label = ui.create_aligned_text(
            digit,
            fontAtlas,
            slot,
            UiAlignment::eCenter,
            24.0f,
            dialog_text_style("#202124FF", 450.0f),
            options.layer + 3,
            slotOptions.order + 1u);
        if (RenderLayer2DComponent* labelLayer =
            ui.registry().try_get<RenderLayer2DComponent>(label))
        {
            labelLayer->visible = filled;
        }
        handle.digitSlots.push_back(slot);
        handle.digitLabels.push_back(label);
    }
    entt::entity inputRoot = ui.scene().create_shape(
        { 0.0f, 0.0f },
        scaled_size(codeWidth, slotHeight, ui.scale()),
        ui_clear_surface_style(),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(inputRoot, 9.0f);
    ui.set_layer(inputRoot, options.layer + 4, options.order + 100u);
    ui.attach_aligned(
        inputRoot,
        handle.root,
        UiAlignment::eTopLeft,
        scaled_offset(codeLeft, codeTop, ui.scale()),
        scaled_size(codeWidth, slotHeight, ui.scale()));
    // Transparent input geometry needs an explicit hit region; otherwise the
    // renderer correctly ignores it during pointer hit testing.
    ui.registry().emplace<HitRegion2DComponent>(inputRoot);
    handle.input = inputRoot;

    TextInputVisualComponent inputVisual = {};
    inputVisual.idle = ui_clear_surface_style();
    inputVisual.focused = ui_clear_surface_style();
    inputVisual.disabled = ui_clear_surface_style();
    inputVisual.hasDisabled = true;
    inputVisual.valueText = dialog_text_style(
        "rgba(0, 0, 0, 0)",
        400.0f);
    inputVisual.placeholderText = inputVisual.valueText;
    inputVisual.caretStyle = ui_clear_surface_style();
    ui.registry().emplace<TextInputVisualComponent>(inputRoot, inputVisual);

    Renderer2DScene* scene = &ui.scene();
    const Renderer2DFontAtlas* atlas = &fontAtlas;
    const std::vector<entt::entity> slots = handle.digitSlots;
    const std::vector<entt::entity> labels = handle.digitLabels;
    TextInputComponent input = {};
    // Slot labels own presentation. The logical input does not need a hidden
    // text or caret entity.
    input.textEntity = entt::null;
    input.value = state->value;
    // Keep enough raw room for formatted pasted values; the callback below
    // filters and truncates the durable value to digitCount immediately.
    input.maxBytes = std::max<std::size_t>(digitCount * 4u, 32u);
    input.onChanged = [
        scene,
        atlas,
        inputRoot,
        slots,
        labels,
        state,
        digitCount,
        onChanged = options.onChanged,
        onCompleted = options.onCompleted](const std::string& rawValue) {
        const std::string value = ui_sanitise_verification_code(
            rawValue,
            digitCount);
        if (TextInputComponent* component =
            scene->registry().try_get<TextInputComponent>(inputRoot))
        {
            component->value = value;
        }
        state->value = value;
        const std::size_t activeIndex = std::min(
            value.size(),
            digitCount - 1u);
        for (std::size_t index = 0u; index < digitCount; ++index)
        {
            const bool filled = index < value.size();
            if (ShapeStyleComponent* style =
                scene->registry().try_get<ShapeStyleComponent>(slots[index]))
            {
                *style = verification_slot_style(
                    filled,
                    index == activeIndex);
                scene->mark_dirty(slots[index]);
            }
            if (RenderLayer2DComponent* labelLayer =
                scene->registry().try_get<RenderLayer2DComponent>(
                    labels[index]))
            {
                labelLayer->visible = filled;
                scene->mark_dirty(labels[index]);
            }
            if (filled)
            {
                set_text_entity(
                    *scene,
                    *atlas,
                    labels[index],
                    std::string(1u, value[index]));
            }
        }
        if (onChanged)
        {
            onChanged(value);
        }
        if (value.size() == digitCount)
        {
            if (!state->completionEmitted && onCompleted)
            {
                state->completionEmitted = true;
                onCompleted(value);
            }
        }
        else
        {
            state->completionEmitted = false;
        }
    };
    ui.registry().emplace<TextInputComponent>(inputRoot, std::move(input));
    ui_update_text_input_visual(ui.scene(), fontAtlas, inputRoot);

    UiButtonOptions resendOptions = {};
    resendOptions.cornerRadius = 8.0f;
    resendOptions.fontSize = 12.0f;
    resendOptions.textColor = "#126BDBFF";
    resendOptions.backgroundColor = "rgba(0, 0, 0, 0)";
    resendOptions.hoveredBackgroundColor = "rgba(18, 107, 219, 0.08)";
    resendOptions.pressedBackgroundColor = "rgba(18, 107, 219, 0.15)";
    resendOptions.outlineColor = "rgba(0, 0, 0, 0)";
    resendOptions.padding = { 8.0f, 2.0f, 8.0f, 2.0f };
    resendOptions.onClick = [action = std::move(options.onResend)](
        const PointerInputEvent&) {
        if (action)
        {
            action();
        }
    };
    ui.create_aligned_text(
        "ⓘ",
        fontAtlas,
        handle.root,
        UiAlignment::eTopLeft,
        12.0f,
        dialog_text_style("rgba(0, 0, 0, 0)", 600.0f),
        options.layer + 2,
        options.order + 110u,
        { 48.0f, 226.0f });
    UiSurfaceBlockOptions infoOptions = {};
    infoOptions.style = make_solid_style("#1677FFFF");
    infoOptions.primitive = Renderer2DPrimitive::eEllipse;
    infoOptions.cornerRadius = 5.0f;
    infoOptions.layer = options.layer + 2;
    infoOptions.order = options.order + 110u;
    const entt::entity info = ui_create_surface_block(
        ui,
        handle.root,
        UiAlignment::eTopLeft,
        { 49.0f, 226.0f },
        { 10.0f, 10.0f },
        infoOptions);
    ui.create_aligned_text(
        "i",
        fontAtlas,
        info,
        UiAlignment::eCenter,
        8.0f,
        dialog_text_style("#FFFFFFFF", 700.0f),
        options.layer + 3,
        options.order + 110u,
        { 0.0f, -0.5f });
    ui_create_aligned_text_button(
        ui,
        fontAtlas,
        handle.root,
        UiAlignment::eTopLeft,
        { 63.0f, 216.0f },
        options.resendLabel,
        { 180.0f, 30.0f },
        options.layer + 2,
        options.order + 111u,
        std::move(resendOptions));

    UiButtonOptions cancelOptions = secondary_button_options(
        std::move(options.onCancel));
    cancelOptions.fontSize = 12.0f;
    cancelOptions.cornerRadius = 18.0f;
    UiControlHandle cancel = ui_create_aligned_text_button(
        ui,
        fontAtlas,
        handle.root,
        UiAlignment::eBottomRight,
        { -22.0f, -18.0f },
        options.cancelLabel,
        { 78.0f, 34.0f },
        options.layer + 2,
        options.order + 112u,
        std::move(cancelOptions));
    handle.cancelButton = cancel.root;
    return handle;
}
