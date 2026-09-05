function(vibrance_add_engine_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE vibrance::engine)

    if(WIN32)
        add_custom_command(
            TARGET ${test_name}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:vibrance::engine>
                $<TARGET_FILE_DIR:${test_name}>)
    endif()

    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

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

# Lottie parsing is intentionally renderer-independent. Compile the private
# importer directly so its generated SVG frames can be checked without a GPU.
add_executable(
    vibrance_lottie_tests
    lottie_tests.cpp
    ../src/vibranceUI/animation/lottie.cpp)
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
