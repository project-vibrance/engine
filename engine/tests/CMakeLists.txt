add_executable(vibrance_notification_record_cache_tests notification_record_cache_tests.cpp)
target_compile_features(vibrance_notification_record_cache_tests PRIVATE cxx_std_20)
target_include_directories(vibrance_notification_record_cache_tests PRIVATE ../include)
add_test(NAME vibrance_notification_record_cache_tests COMMAND vibrance_notification_record_cache_tests)

if(VIBRANCE_RENDER_BACKEND STREQUAL "Vulkan")
    add_executable(vibrance_liquid_rim_shader_tests liquid_rim_shader_tests.cpp)
    target_compile_features(vibrance_liquid_rim_shader_tests PRIVATE cxx_std_20)
    target_include_directories(vibrance_liquid_rim_shader_tests PRIVATE ../include "${GLM_INCLUDE_DIR}")
    target_link_libraries(vibrance_liquid_rim_shader_tests PRIVATE Vulkan::Vulkan)
    add_test(NAME vibrance_liquid_rim_shader_tests COMMAND vibrance_liquid_rim_shader_tests
        "${PROJECT_BINARY_DIR}/engine/generated/spirv/blur_2d.comp.spv"
        "${PROJECT_BINARY_DIR}/engine/generated/spirv/shape_2d.comp.spv")
    set_tests_properties(vibrance_liquid_rim_shader_tests PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(WIN32)
    add_executable(vibrance_liquid_runtime_identity_tests liquid_runtime_identity_tests.cpp)
    target_compile_features(vibrance_liquid_runtime_identity_tests PRIVATE cxx_std_20)
    target_link_libraries(vibrance_liquid_runtime_identity_tests PRIVATE bcrypt)
    add_test(NAME vibrance_liquid_runtime_identity_tests COMMAND vibrance_liquid_runtime_identity_tests
        "${CMAKE_CURRENT_BINARY_DIR}/liquid-runtime-identity-fixture")
    if(VIBRANCE_WIN32_COMPOSITION_AVAILABLE)
        add_test(NAME vibrance_locked_liquid_runtime_tests
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/locked_liquid_runtime_tests.ps1"
                -DeployScript "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/VibranceLockedRuntime.cmake"
                -Broker "${VIBRANCE_LIQUID_DWM_BROKER}"
                -DbgHelp "${VIBRANCE_LIQUID_DBGHELP_RUNTIME}"
                -SymSrv "${VIBRANCE_LIQUID_SYMSRV_RUNTIME}"
                -OutputDirectory "${CMAKE_CURRENT_BINARY_DIR}/locked-runtime-fixture")
    endif()
    # Refresh the DLL even when the import library did not change and tests do
    # not relink. All engine tests share one runtime directory.
    add_custom_target(vibrance_engine_test_runtime ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vibrance::engine>
            $<TARGET_FILE_DIR:vibrance_countdown_timer_tests>
        VERBATIM)
    add_dependencies(vibrance_engine_test_runtime vibrance_engine)
endif()

function(vibrance_add_engine_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE vibrance::engine)

    if(WIN32)
        add_dependencies(${test_name} vibrance_engine_test_runtime)
    endif()

    add_test(NAME ${test_name} COMMAND ${test_name} ${ARGN})
endfunction()

if(NOT APPLE)
    # Deterministic VMA/Vulkan failures exercise the production image owner
    # without reserving graphics memory or creating a physical device.
    add_executable(vibrance_image_allocation_failure_tests
        image_allocation_failure_tests.cpp ../src/renderer/vulkan/image.cpp)
    target_link_libraries(vibrance_image_allocation_failure_tests PRIVATE
        vibrance::engine Vulkan::Vulkan)
    if(WIN32)
        target_compile_definitions(vibrance_image_allocation_failure_tests PRIVATE VK_USE_PLATFORM_WIN32_KHR)
        add_dependencies(vibrance_image_allocation_failure_tests vibrance_engine_test_runtime)
    endif()
    add_test(NAME vibrance_image_allocation_failure_tests COMMAND vibrance_image_allocation_failure_tests)
endif()

vibrance_add_engine_test(vibrance_logger_tests logger_tests.cpp)
vibrance_add_engine_test(vibrance_media_preparation_tests media_preparation_tests.cpp)
add_test(NAME vibrance_media_preparation_renderer_tests COMMAND vibrance_media_preparation_tests --renderer)
set_tests_properties(vibrance_media_preparation_renderer_tests PROPERTIES SKIP_RETURN_CODE 77)
add_executable(vibrance_media_upload_retirement_tests media_upload_retirement_tests.cpp)
target_compile_features(vibrance_media_upload_retirement_tests PRIVATE cxx_std_20)
add_test(NAME vibrance_media_upload_retirement_tests COMMAND vibrance_media_upload_retirement_tests)
if(NOT APPLE)
    vibrance_add_engine_test(vibrance_media_upload_fence_tests media_upload_fence_tests.cpp)
    target_link_libraries(vibrance_media_upload_fence_tests PRIVATE Vulkan::Vulkan)
    if(WIN32)
        # VMA's function table includes Win32 external-memory functions when
        # the engine enables this platform. Keep the fixture allocator ABI equal.
        target_compile_definitions(vibrance_media_upload_fence_tests PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    endif()
    set_tests_properties(vibrance_media_upload_fence_tests PROPERTIES SKIP_RETURN_CODE 77)
endif()
vibrance_add_engine_test(vibrance_media_timeline_range_tests media_timeline_range_tests.cpp)
vibrance_add_engine_test(vibrance_layout_direction_tests layout_direction_tests.cpp)
add_executable(vibrance_text_direction_tests text_direction_tests.cpp
    ../src/renderer/common/text_direction.cpp)
add_test(NAME vibrance_text_direction_tests COMMAND vibrance_text_direction_tests)
add_executable(vibrance_runtime_log_storage_tests runtime_log_storage_tests.cpp)
target_include_directories(vibrance_runtime_log_storage_tests PRIVATE ../include)
add_test(NAME vibrance_runtime_log_storage_tests COMMAND vibrance_runtime_log_storage_tests)
if(WIN32)
    add_test(NAME vibrance_crash_diagnostics_tests COMMAND ${CMAKE_COMMAND}
        "-DLOGGER_TEST=$<TARGET_FILE:vibrance_logger_tests>"
        "-DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/crash-diagnostics"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_crash_diagnostics.cmake")
endif()

set(_vibrance_test_fonts "${CMAKE_SOURCE_DIR}/../vibrance-app/app/assets/fonts")
if(EXISTS "${CMAKE_SOURCE_DIR}/../app-private/app/assets/fonts/Inter.ttc")
    set(_vibrance_test_fonts "${CMAKE_SOURCE_DIR}/../app-private/app/assets/fonts")
endif()
# Migrate the previous sibling default while preserving custom test fonts.
if(VIBRANCE_EMOJI_TEST_FONT STREQUAL "${CMAKE_SOURCE_DIR}/../vibrance-app/app/assets/fonts/AppleColorEmoji.ttf" AND
   NOT EXISTS "${VIBRANCE_EMOJI_TEST_FONT}")
    unset(VIBRANCE_EMOJI_TEST_FONT CACHE)
endif()
set(VIBRANCE_EMOJI_TEST_FONT "${_vibrance_test_fonts}/AppleColorEmoji.ttf"
    CACHE FILEPATH "Colour emoji font used by the shaping and bitmap regression test")
vibrance_add_engine_test(vibrance_emoji_font_tests emoji_font_tests.cpp "${VIBRANCE_EMOJI_TEST_FONT}")
target_link_libraries(vibrance_emoji_font_tests PRIVATE vibrance_harfbuzz)
target_include_directories(vibrance_emoji_font_tests PRIVATE "${STB_INCLUDE_DIR}")
target_sources(vibrance_emoji_font_tests PRIVATE ../src/renderer/common/text_direction.cpp)
set_tests_properties(vibrance_emoji_font_tests PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME vibrance_emoji_renderer_tests COMMAND vibrance_emoji_font_tests
    "${VIBRANCE_EMOJI_TEST_FONT}" --renderer)
set_tests_properties(vibrance_emoji_renderer_tests PROPERTIES SKIP_RETURN_CODE 77)

vibrance_add_engine_test(vibrance_system_backdrop_policy_tests system_backdrop_policy_tests.cpp)
add_executable(vibrance_liquid_dwm_windows10_layout_tests liquid_dwm_windows10_layout_tests.cpp)
target_compile_features(vibrance_liquid_dwm_windows10_layout_tests PRIVATE cxx_std_20)
add_test(NAME vibrance_liquid_dwm_windows10_layout_tests COMMAND vibrance_liquid_dwm_windows10_layout_tests)
add_executable(vibrance_liquid_dwm_windows10_lifecycle_tests liquid_dwm_windows10_lifecycle_tests.cpp)
target_compile_features(vibrance_liquid_dwm_windows10_lifecycle_tests PRIVATE cxx_std_20)
add_test(NAME vibrance_liquid_dwm_windows10_lifecycle_tests COMMAND vibrance_liquid_dwm_windows10_lifecycle_tests)
add_executable(vibrance_liquid_dwm_config_tests liquid_dwm_config_tests.cpp)
target_compile_features(vibrance_liquid_dwm_config_tests PRIVATE cxx_std_20)
add_test(NAME vibrance_liquid_dwm_config_tests COMMAND vibrance_liquid_dwm_config_tests)
add_executable(vibrance_composition_region_effects_tests composition_region_effects_tests.cpp)
target_compile_features(vibrance_composition_region_effects_tests PRIVATE cxx_std_20)
add_test(NAME vibrance_composition_region_effects_tests COMMAND vibrance_composition_region_effects_tests)
add_executable(vibrance_composition_present_retry_tests composition_present_retry_tests.cpp)
target_compile_features(vibrance_composition_present_retry_tests PRIVATE cxx_std_20)
add_test(NAME vibrance_composition_present_retry_tests COMMAND vibrance_composition_present_retry_tests)
add_executable(vibrance_composition_readback_tests composition_readback_tests.cpp)
target_compile_features(vibrance_composition_readback_tests PRIVATE cxx_std_20)
add_test(NAME vibrance_composition_readback_tests COMMAND vibrance_composition_readback_tests)
add_executable(vibrance_composition_upload_tests composition_upload_tests.cpp)
target_compile_features(vibrance_composition_upload_tests PRIVATE cxx_std_20)
add_test(NAME vibrance_composition_upload_tests COMMAND vibrance_composition_upload_tests)
if(VIBRANCE_WIN32_COMPOSITION_AVAILABLE)
    set(_vibrance_hook_test "${CMAKE_CURRENT_BINARY_DIR}/vibrance_liquid_dwm_hook_tests.exe")
    add_custom_command(OUTPUT "${_vibrance_hook_test}"
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/win32/build_dwm_hook_tests.bat"
            "${VIBRANCE_VCVARSALL}" "${VIBRANCE_COMPOSITION_VCVARS_ARCH}"
            "${CMAKE_CURRENT_SOURCE_DIR}/liquid_dwm_hook_tests.cpp"
            "${VIBRANCE_COMPOSITION_DIRECTORY}/dwm-extension-obj"
            "${_vibrance_hook_test}" "${CMAKE_CURRENT_BINARY_DIR}/dwm-hook-test-obj"
            "${VIBRANCE_COMPOSITION_SDK_INCLUDE_ROOT}"
            "${VIBRANCE_COMPOSITION_SDK_UM_LIBRARY_DIR}"
            "${VIBRANCE_COMPOSITION_SDK_UCRT_LIBRARY_DIR}"
        DEPENDS liquid_dwm_hook_tests.cpp "${VIBRANCE_LIQUID_DWM_EXTENSION}"
            "${PROJECT_SOURCE_DIR}/scripts/win32/build_dwm_hook_tests.bat"
        VERBATIM)
    add_custom_target(vibrance_liquid_dwm_hook_fixture ALL DEPENDS "${_vibrance_hook_test}")
    add_dependencies(vibrance_liquid_dwm_hook_fixture vibrance_liquid_dwm_extension)
    if(TARGET vibrance_liquid_glass_shader)
        add_dependencies(vibrance_liquid_dwm_hook_fixture vibrance_liquid_glass_shader
            vibrance_liquid_glass_compare_shader
            vibrance_liquid_glass_ui_shader vibrance_liquid_glass_fullscreen_shader
            vibrance_liquid_glass_blur_shaders)
    endif()
    add_test(NAME vibrance_liquid_dwm_hook_tests COMMAND "${_vibrance_hook_test}")
endif()
vibrance_add_engine_test(vibrance_playback_quality_tests playback_quality_tests.cpp)
if(VIBRANCE_WIN32_INTEROP_AVAILABLE)
    # Exercise the native cached UIA worker with a controlled playback window,
    # without relying on installed players or changing the user's audio.
    set(_vibrance_quality_test "${CMAKE_CURRENT_BINARY_DIR}/vibrance_playback_quality_win32_tests.exe")
    add_custom_command(OUTPUT "${_vibrance_quality_test}"
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/win32/build_native_test.bat"
            "${VIBRANCE_VCVARSALL}" "${VIBRANCE_WIN32_INTEROP_ARCH}"
            "${CMAKE_CURRENT_SOURCE_DIR}/playback_quality_win32_tests.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include"
            "${_vibrance_quality_test}" "${CMAKE_CURRENT_BINARY_DIR}/quality-probe-obj"
        DEPENDS playback_quality_win32_tests.cpp
            ../src/platform/win32/playback_quality_probe.h
            ../src/media/playback_quality_detection.h
            ../include/vibranceUI/media/playback_quality.h
            "${PROJECT_SOURCE_DIR}/scripts/win32/build_native_test.bat"
        VERBATIM)
    add_custom_target(vibrance_playback_quality_win32_fixture ALL DEPENDS "${_vibrance_quality_test}")
    add_test(NAME vibrance_playback_quality_win32_tests COMMAND "${_vibrance_quality_test}")
endif()
vibrance_add_engine_test(vibrance_animated_control_masks_tests animated_control_masks_tests.cpp)
vibrance_add_engine_test(vibrance_scene_cache_ownership_tests scene_cache_ownership_tests.cpp)
vibrance_add_engine_test(vibrance_horizontal_scroll_fade_tests horizontal_scroll_fade_tests.cpp)
vibrance_add_engine_test(vibrance_viewport_fade_tests viewport_fade_tests.cpp)
if(VIBRANCE_RENDER_BACKEND STREQUAL "Vulkan")
    add_executable(vibrance_media_pipeline_policy_tests media_pipeline_policy_tests.cpp)
    target_compile_features(vibrance_media_pipeline_policy_tests PRIVATE cxx_std_20)
    add_test(NAME vibrance_media_pipeline_policy_tests COMMAND vibrance_media_pipeline_policy_tests)
    add_executable(vibrance_pipeline_cache_store_tests pipeline_cache_store_tests.cpp)
    target_compile_features(vibrance_pipeline_cache_store_tests PRIVATE cxx_std_20)
    add_test(NAME vibrance_pipeline_cache_store_tests COMMAND vibrance_pipeline_cache_store_tests
        "${CMAKE_CURRENT_BINARY_DIR}/pipeline-cache-store")
    add_executable(vibrance_viewport_fade_shader_tests viewport_fade_shader_tests.cpp)
    target_include_directories(vibrance_viewport_fade_shader_tests PRIVATE ../include "${GLM_INCLUDE_DIR}")
    target_link_libraries(vibrance_viewport_fade_shader_tests PRIVATE Vulkan::Vulkan)
    add_dependencies(vibrance_viewport_fade_shader_tests vibrance_engine)
    add_test(NAME vibrance_viewport_fade_shader_tests COMMAND vibrance_viewport_fade_shader_tests
        "${CMAKE_CURRENT_BINARY_DIR}/../generated/spirv/composite_2d.comp.spv")
    add_executable(vibrance_panel_clip_shader_tests panel_clip_shader_tests.cpp)
    target_compile_features(vibrance_panel_clip_shader_tests PRIVATE cxx_std_20)
    target_link_libraries(vibrance_panel_clip_shader_tests PRIVATE Vulkan::Vulkan)
    add_dependencies(vibrance_panel_clip_shader_tests vibrance_engine)
    add_test(NAME vibrance_panel_clip_shader_tests COMMAND vibrance_panel_clip_shader_tests
        "${CMAKE_CURRENT_BINARY_DIR}/../generated/spirv/shape_2d.comp.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/../generated/spirv/shadow_2d.comp.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/../generated/spirv/blur_2d.comp.spv")
    set_tests_properties(vibrance_panel_clip_shader_tests PROPERTIES TIMEOUT 90)
endif()
vibrance_add_engine_test(vibrance_digit_spacing_tests digit_spacing_tests.cpp
    "${_vibrance_test_fonts}/Inter.ttc"
    "${_vibrance_test_fonts}/NotoSans-Variable.ttf")
set_tests_properties(vibrance_digit_spacing_tests PROPERTIES SKIP_RETURN_CODE 77)
vibrance_add_engine_test(vibrance_text_baseline_tests text_baseline_tests.cpp
    "${_vibrance_test_fonts}/Inter.ttc"
    "${_vibrance_test_fonts}/NotoSans-Variable.ttf"
    "${_vibrance_test_fonts}/../svg/static/icons/player/dolby_atmos.svg"
    "${_vibrance_test_fonts}/../svg/static/icons/player/lossless.svg")
set_tests_properties(vibrance_text_baseline_tests PROPERTIES SKIP_RETURN_CODE 77)

vibrance_add_engine_test(
    vibrance_countdown_timer_tests
    countdown_timer_tests.cpp)
vibrance_add_engine_test(
    vibrance_popup_controls_tests
    popup_controls_tests.cpp)
vibrance_add_engine_test(
    vibrance_simple_ui_builder_tests
    simple_ui_builder_tests.cpp)
vibrance_add_engine_test(
    vibrance_dynamic_coverage_tests
    dynamic_coverage_tests.cpp)
vibrance_add_engine_test(
    vibrance_entity_bounds_tests
    entity_bounds_tests.cpp)

vibrance_add_engine_test(
    vibrance_liquid_output_bounds_tests
    liquid_output_bounds_tests.cpp)
vibrance_add_engine_test(
    vibrance_control_interaction_tests
    control_interaction_tests.cpp)
vibrance_add_engine_test(
    vibrance_scroll_viewport_tests
    scroll_viewport_tests.cpp)
vibrance_add_engine_test(
    vibrance_navigation_history_tests
    navigation_history_tests.cpp)
vibrance_add_engine_test(
    vibrance_page_stack_tests
    page_stack_tests.cpp)
vibrance_add_engine_test(
    vibrance_text_block_tests
    text_block_tests.cpp)
vibrance_add_engine_test(
    vibrance_stretch_dynamics_tests
    stretch_dynamics_tests.cpp)
vibrance_add_engine_test(
    vibrance_single_instance_tests
    single_instance_tests.cpp)
vibrance_add_engine_test(
    vibrance_secure_credential_tests
    secure_credential_tests.cpp)
vibrance_add_engine_test(
    vibrance_system_theme_tests
    system_theme_tests.cpp)
vibrance_add_engine_test(
    vibrance_media_presentation_tests
    media_presentation_tests.cpp)

# Text dependency planning needs neither a GPU nor the engine runtime.
add_executable(vibrance_text_batch_tests text_batch_tests.cpp)
add_test(NAME vibrance_text_batch_tests COMMAND vibrance_text_batch_tests)
add_executable(vibrance_render_op_order_tests render_op_order_tests.cpp)
add_test(NAME vibrance_render_op_order_tests COMMAND vibrance_render_op_order_tests)

# Timestamp arithmetic is independent of the renderer and GPU. Keep this target
# standalone so counter wrap and clock support checks need no engine runtime.
add_executable(
    vibrance_timestamp_diagnostics_tests
    timestamp_diagnostics_tests.cpp)
add_test(
    NAME vibrance_timestamp_diagnostics_tests
    COMMAND vibrance_timestamp_diagnostics_tests)

# Lottie parsing is intentionally renderer-independent. Compile the private
# importer directly so its generated SVG frames can be checked without a GPU.
add_executable(
    vibrance_lottie_tests
    lottie_tests.cpp
    ../src/animation/lottie.cpp)
target_link_libraries(
    vibrance_lottie_tests
    PRIVATE simdjson::simdjson)
add_test(
    NAME vibrance_lottie_tests
    COMMAND vibrance_lottie_tests)

# Compile and run the non-Windows provider and calling-convention branches even
# on Windows. This target owns its provider implementation so its force-portable
# definitions cannot be hidden by the normally compiled engine DLL.
add_executable(
    vibrance_media_session_portable_tests
    media_session_portable_tests.cpp
    ../src/media/session.cpp)
target_include_directories(
    vibrance_media_session_portable_tests
    PRIVATE ../include)
target_compile_definitions(
    vibrance_media_session_portable_tests
    PRIVATE
        VIBRANCE_ENGINE_EXPORTS=1
        VIBRANCE_FORCE_PORTABLE_MEDIA_SESSION=1
        VIBRANCE_FORCE_PORTABLE_WIN32_INTEROP_ABI=1)
add_test(
    NAME vibrance_media_session_portable_tests
    COMMAND vibrance_media_session_portable_tests)

vibrance_add_engine_test(
    vibrance_media_session_runtime_tests
    media_session_runtime_tests.cpp)
if(VIBRANCE_WIN32_INTEROP_AVAILABLE)
    add_custom_command(
        TARGET vibrance_media_session_runtime_tests
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VIBRANCE_WIN32_INTEROP_DLL}"
            $<TARGET_FILE_DIR:vibrance_media_session_runtime_tests>)
endif()

# Keep the provider's portable path continuously buildable on every host. The
# implementation is compiled into this target so its feature guard is local to
# the test rather than the shared engine target.
add_executable(
    vibrance_notification_provider_portable_tests
    notification_provider_portable_tests.cpp
    ../src/notifications/provider.cpp)
target_include_directories(
    vibrance_notification_provider_portable_tests
    PRIVATE ../include)
target_compile_definitions(
    vibrance_notification_provider_portable_tests
    PRIVATE
        VIBRANCE_ENGINE_EXPORTS=1
        VIBRANCE_FORCE_PORTABLE_NOTIFICATION_PROVIDER=1
        VIBRANCE_FORCE_PORTABLE_WIN32_INTEROP_ABI=1)
add_test(
    NAME vibrance_notification_provider_portable_tests
    COMMAND vibrance_notification_provider_portable_tests)

if(VIBRANCE_WIN32_INTEROP_AVAILABLE AND VIBRANCE_WIN32_COMPOSITION_AVAILABLE)
    set(_activation_fixture "${CMAKE_CURRENT_BINARY_DIR}/notification_activation_win32_tests.exe")
    add_custom_command(OUTPUT "${_activation_fixture}"
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/win32/build_notification_activation_tests.bat"
            "${VIBRANCE_VCVARSALL}" "${VIBRANCE_COMPOSITION_VCVARS_ARCH}"
            "${CMAKE_CURRENT_SOURCE_DIR}/notification_activation_win32_tests.cpp" "${_activation_fixture}"
            "${VIBRANCE_COMPOSITION_SDK_INCLUDE_ROOT}" "${VIBRANCE_COMPOSITION_SDK_UM_LIBRARY_DIR}"
            "${VIBRANCE_COMPOSITION_SDK_UCRT_LIBRARY_DIR}"
        DEPENDS notification_activation_win32_tests.cpp
            "${PROJECT_SOURCE_DIR}/engine/src/platform/win32/notification_activation.h"
            "${PROJECT_SOURCE_DIR}/engine/src/platform/win32/notification_payload_store.h"
            "${PROJECT_SOURCE_DIR}/scripts/win32/build_notification_activation_tests.bat" VERBATIM)
    add_custom_target(vibrance_notification_activation_fixture ALL DEPENDS "${_activation_fixture}")
    add_test(NAME vibrance_notification_activation_win32_tests COMMAND "${_activation_fixture}")
endif()

vibrance_add_engine_test(
    vibrance_notification_provider_runtime_tests
    notification_provider_runtime_tests.cpp)
if(VIBRANCE_WIN32_INTEROP_AVAILABLE)
    add_custom_command(
        TARGET vibrance_notification_provider_runtime_tests
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VIBRANCE_WIN32_INTEROP_DLL}"
            $<TARGET_FILE_DIR:vibrance_notification_provider_runtime_tests>)
endif()

vibrance_add_engine_test(
    vibrance_notification_stack_layout_tests
    notification_stack_layout_tests.cpp)
vibrance_add_engine_test(
    vibrance_notification_card_tests
    notification_card_tests.cpp)

vibrance_add_engine_test(
    vibrance_display_placement_tests
    display_placement_tests.cpp)
vibrance_add_engine_test(
    vibrance_audio_spectrum_tests
    audio_spectrum_tests.cpp)
vibrance_add_engine_test(
    vibrance_audio_loopback_capture_tests
    audio_loopback_capture_tests.cpp)
if(WIN32)
    add_test(NAME vibrance_audio_loopback_format_tests
        COMMAND vibrance_audio_loopback_capture_tests --process-format)
    set_tests_properties(vibrance_audio_loopback_format_tests PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 20)
endif()

vibrance_add_engine_test(vibrance_file_tests file_tests.cpp)
if(SOLOUD_FOUND)
    vibrance_add_engine_test(vibrance_audio_engine_tests audio_engine_tests.cpp)
endif()
if(WIN32)
    vibrance_add_engine_test(vibrance_glfw_panel_initialisation_tests glfw_panel_initialisation_tests.cpp)
    target_link_libraries(vibrance_glfw_panel_initialisation_tests PRIVATE user32)
    add_test(NAME vibrance_glfw_panel_initialisation_renderer_tests
        COMMAND vibrance_glfw_panel_initialisation_tests --renderer)
    set_tests_properties(vibrance_glfw_panel_initialisation_tests
        vibrance_glfw_panel_initialisation_renderer_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
    vibrance_add_engine_test(vibrance_glfw_panel_drag_tests glfw_panel_drag_tests.cpp)
    target_link_libraries(vibrance_glfw_panel_drag_tests PRIVATE user32 comctl32)
    set_tests_properties(vibrance_glfw_panel_drag_tests PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 30 RESOURCE_LOCK desktop_cursor)
    vibrance_add_engine_test(vibrance_glfw_event_wait_tests glfw_event_wait_tests.cpp)
    target_link_libraries(vibrance_glfw_event_wait_tests PRIVATE user32)
    set_tests_properties(vibrance_glfw_event_wait_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 15)
    vibrance_add_engine_test(vibrance_native_tray_tests native_tray_tests.cpp)
    target_link_libraries(vibrance_native_tray_tests PRIVATE user32)
endif()

add_executable(vibrance_native_tray_portable_tests
    native_tray_portable_tests.cpp
    ../src/platform/portable/native_tray.cpp
    ../src/platform/portable/composition_presenter.cpp)
target_include_directories(vibrance_native_tray_portable_tests PRIVATE
    ../include "${GLM_INCLUDE_DIR}" "${Vulkan_INCLUDE_DIRS}")
target_compile_definitions(vibrance_native_tray_portable_tests PRIVATE
    VIBRANCE_ENGINE_EXPORTS=1 VIBRANCE_FORCE_PORTABLE_NATIVE_TRAY=1
    VIBRANCE_FORCE_PORTABLE_COMPOSITION=1)
add_test(NAME vibrance_native_tray_portable_tests COMMAND vibrance_native_tray_portable_tests)

if(APPLE)
    # Maintainer machines can detect stale MSL without adding shader compiler
    # dependencies to SDK consumers or normal macOS builds.
    find_package(Python3 QUIET COMPONENTS Interpreter)
    find_program(VIBRANCE_GLSLC glslc)
    find_program(VIBRANCE_SPIRV_CROSS spirv-cross)
    if(Python3_Interpreter_FOUND AND VIBRANCE_GLSLC AND VIBRANCE_SPIRV_CROSS)
        add_test(NAME vibrance_metal_shader_sync
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/generate_metal_shaders.py" --check)
    endif()
    add_executable(vibrance_metal_device_tests metal_device_tests.cpp
        ../src/renderer/metal/device.cpp "${METAL_SHADER_CPP}")
    target_include_directories(vibrance_metal_device_tests PRIVATE ../include
        "${METAL_CPP_INCLUDE_DIR}" "${GLM_INCLUDE_DIR}" "${ENTT_INCLUDE_DIR}")
    target_compile_options(vibrance_metal_device_tests PRIVATE -fblocks)
    target_link_libraries(vibrance_metal_device_tests PRIVATE
        "-framework Foundation" "-framework Metal" "-framework QuartzCore")
    add_test(NAME vibrance_metal_device_tests COMMAND vibrance_metal_device_tests)
    set_tests_properties(vibrance_metal_device_tests PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(APPLE)
    vibrance_add_engine_test(vibrance_metal_window_tests metal_window_tests.cpp)
    vibrance_add_engine_test(vibrance_macos_panel_drag_cursor_tests macos_panel_drag_cursor_tests.mm)
    target_link_libraries(vibrance_macos_panel_drag_cursor_tests PRIVATE "-framework AppKit")
    set_tests_properties(vibrance_macos_panel_drag_cursor_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 15)
    vibrance_add_engine_test(vibrance_native_glass_controls_tests native_glass_controls_tests.mm)
    target_compile_options(vibrance_native_glass_controls_tests PRIVATE -fobjc-arc)
    target_link_libraries(vibrance_native_glass_controls_tests PRIVATE "-framework AppKit" "-framework QuartzCore")
    set_tests_properties(vibrance_native_glass_controls_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
    set_tests_properties(vibrance_metal_window_tests PROPERTIES SKIP_RETURN_CODE 77)
    vibrance_add_engine_test(vibrance_metal_frame_damage_tests metal_frame_damage_tests.cpp)
endif()

if(APPLE)
    add_executable(vibrance_media_session_macos_tests media_session_macos_tests.mm)
    target_include_directories(vibrance_media_session_macos_tests PRIVATE ../include)
    target_compile_options(vibrance_media_session_macos_tests PRIVATE -fobjc-arc)
    target_link_libraries(vibrance_media_session_macos_tests PRIVATE
        "-framework Cocoa" "-framework ApplicationServices")
    add_test(NAME vibrance_media_session_macos_tests COMMAND vibrance_media_session_macos_tests)
endif()

if(APPLE)
    # Isolated companion fixtures exercise process I/O without controlling the
    # user's real player. The production provider resolves beside this binary.
    add_executable(vibrance_media_session_macos_provider_tests
        media_session_macos_provider_tests.mm ../src/media/session_macos.mm)
    set_target_properties(vibrance_media_session_macos_provider_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/media-session-fixture")
    target_include_directories(vibrance_media_session_macos_provider_tests PRIVATE ../include)
    target_compile_options(vibrance_media_session_macos_provider_tests PRIVATE -fobjc-arc)
    target_link_libraries(vibrance_media_session_macos_provider_tests PRIVATE
        "-framework Cocoa" "-framework ApplicationServices")
    add_test(NAME vibrance_media_session_macos_provider_tests
        COMMAND vibrance_media_session_macos_provider_tests)
endif()

if(APPLE)
    add_executable(vibrance_audio_loopback_macos_tests audio_loopback_macos_tests.cpp)
    add_test(NAME vibrance_audio_loopback_macos_tests COMMAND vibrance_audio_loopback_macos_tests)
endif()
