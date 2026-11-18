# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

set(ENGINE_SOURCES
    src/renderer/common/scene.cpp
    src/renderer/common/font_atlas.cpp
    src/renderer/common/media2d.cpp
    src/renderer/common/text_direction.cpp
    src/animation/lottie.cpp
    src/audio/audio.cpp
    src/audio/loopback_capture.cpp
    src/audio/spectrum.cpp
    src/core/camera.cpp
    src/core/file.cpp
    src/core/logger.cpp
    src/core/liquid_glass_runtime.cpp
    src/core/process.cpp
    src/core/secure_credential.cpp
    src/core/single_instance.cpp
    src/display/foreground.cpp
    src/display/placement.cpp
    src/display/preferences.cpp
    src/glfw/panel_window.cpp
    src/glfw/ui_surface.cpp
    src/glfw/ui_window.cpp
    src/glfw/window.cpp
    src/graphics/backdrop.cpp
    src/graphics/colour_palette.cpp
    src/platform/image_services.cpp
    src/ui/colour_palette_panel.cpp
    src/localisation/localisation.cpp
    src/media/artwork.cpp
    src/media/artwork_view.cpp
    src/media/presentation.cpp
    src/media/session.cpp
    src/notifications/card.cpp
    src/notifications/text.cpp
    src/notifications/provider.cpp
    src/notifications/stack_layout.cpp
    src/platform/native_dialog.cpp
    src/renderer/common/font_file.cpp
    src/time/countdown_timer.cpp
    src/ui/animated_text_view.cpp
    src/ui/context_menu.cpp
    src/ui/dialog_controls.cpp
    src/ui/modal_lifecycle.cpp
    src/ui/resource_loading.cpp
    src/ui/theme.cpp
    src/ui/text_editing.cpp
)
if(APPLE)
    list(APPEND ENGINE_SOURCES
        src/platform/macos/native_files.mm
        src/platform/macos/font_file.mm
        src/platform/macos/image_services.mm
        src/platform/macos/desktop_wallpaper.mm
        src/renderer/metal/renderer.cpp
        src/renderer/metal/device.cpp
        src/renderer/metal/surface.mm
        src/renderer/metal/native_glass.mm
        src/renderer/metal/mesh.cpp)
    set_source_files_properties(src/platform/macos/native_files.mm src/platform/macos/font_file.mm src/platform/macos/image_services.mm src/platform/macos/desktop_wallpaper.mm src/renderer/metal/native_glass.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
else()
    list(APPEND ENGINE_SOURCES src/core/vma_usage.cpp src/factories/mesh_factory.cpp
        src/renderer/vulkan/buffer.cpp
        src/renderer/vulkan/command.cpp
        src/renderer/vulkan/descriptors.cpp
        src/renderer/vulkan/device.cpp
        src/renderer/vulkan/frame.cpp
        src/renderer/vulkan/image.cpp
        src/renderer/vulkan/instance.cpp
        src/renderer/vulkan/renderer.cpp
        src/renderer/vulkan/renderer2d.cpp
        src/renderer/vulkan/renderer3d.cpp
        src/renderer/vulkan/shader.cpp
        src/renderer/vulkan/swapchain.cpp
        src/renderer/vulkan/synchronisation.cpp
    )
endif()




if(WIN32)
    list(APPEND ENGINE_SOURCES
        src/platform/win32/desktop_wallpaper.cpp
        src/platform/win32/font_file.cpp
        src/platform/win32/image_services.cpp
        src/platform/win32/native_files.cpp
        src/platform/win32/native_tray.cpp
        src/platform/win32/composition_presenter.cpp)
elseif(APPLE)
    list(APPEND ENGINE_SOURCES src/glfw/window_macos.mm src/platform/macos/native_tray.mm
        src/media/session_macos.mm src/audio/loopback_capture_macos.mm)
    set_source_files_properties(src/media/session_macos.mm src/audio/loopback_capture_macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
else()
    list(APPEND ENGINE_SOURCES
        src/platform/portable/desktop_wallpaper.cpp
        src/platform/portable/image_services.cpp
        src/platform/portable/native_files.cpp
        src/platform/portable/native_tray.cpp)
endif()
if(NOT WIN32 AND NOT APPLE)
    list(APPEND ENGINE_SOURCES src/platform/portable/composition_presenter.cpp)
endif()
