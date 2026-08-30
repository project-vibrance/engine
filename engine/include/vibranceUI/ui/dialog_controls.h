#pragma once

#include <vibranceUI/export.h>
#include <vibranceUI/ui/builder.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct UiConfirmationDialogOptions
{
    glm::vec2 size { 520.0f, 464.0f };
    std::string title = "Are you sure?";
    std::string message {};
    std::string cancelLabel = "Cancel";
    std::string confirmLabel = "Confirm";
    bool destructive = false;
    std::function<void()> onCancel {};
    std::function<void()> onConfirm {};
    int32_t layer = 20;
    uint32_t order = 0u;
};

struct UiConfirmationDialogHandle
{
    entt::entity root = entt::null;
    entt::entity iconSlot = entt::null;
    entt::entity cancelButton = entt::null;
    entt::entity confirmButton = entt::null;
};

VIBRANCE_ENGINE_API UiConfirmationDialogHandle
ui_create_confirmation_dialog(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    UiConfirmationDialogOptions options = {});

struct UiVerificationCodeState
{
    std::string value {};
    bool completionEmitted = false;
};

struct UiVerificationCodeOptions
{
    glm::vec2 size { 500.0f, 410.0f };
    std::size_t digitCount = 6u;
    std::string title = "Two-Factor Authentication";
    std::string message = "Please enter the verification code sent to\nyour phone.";
    std::string resendLabel = "Did not receive a code?";
    std::string cancelLabel = "Cancel";
    std::function<void(std::string)> onChanged {};
    std::function<void(std::string)> onCompleted {};
    std::function<void()> onResend {};
    std::function<void()> onCancel {};
    int32_t layer = 20;
    uint32_t order = 0u;
};

struct UiVerificationCodeHandle
{
    entt::entity root = entt::null;
    entt::entity input = entt::null;
    entt::entity cancelButton = entt::null;
    std::vector<entt::entity> digitSlots {};
    std::vector<entt::entity> digitLabels {};
};

VIBRANCE_ENGINE_API std::string ui_sanitise_verification_code(
    std::string_view value,
    std::size_t digitCount);

VIBRANCE_ENGINE_API UiVerificationCodeHandle
ui_create_verification_code_dialog(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    std::shared_ptr<UiVerificationCodeState> state,
    UiVerificationCodeOptions options = {});
