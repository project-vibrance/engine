# Keep this companion independent of the engine: Apple-signed Perl hosts it
# because recent macOS releases restrict MediaRemote reads in ordinary apps.
enable_language(OBJC)
if(NOT MEDIAREMOTE_ADAPTER_ROOT OR
   NOT EXISTS "${MEDIAREMOTE_ADAPTER_ROOT}/include/MediaRemoteAdapter.h" OR
   NOT EXISTS "${MEDIAREMOTE_ADAPTER_ROOT}/bin/mediaremote-adapter.pl" OR
   NOT EXISTS "${MEDIAREMOTE_ADAPTER_ROOT}/LICENSE")
    message(FATAL_ERROR
        "Set MEDIAREMOTE_ADAPTER_ROOT in .env.cmake to the mediaremote-adapter source directory.")
endif()
set(_media_adapter "${MEDIAREMOTE_ADAPTER_ROOT}")
file(GLOB_RECURSE _media_adapter_sources CONFIGURE_DEPENDS
    "${_media_adapter}/src/adapter/*.m"
    "${_media_adapter}/src/private/*.m"
    "${_media_adapter}/src/utility/*.m")
add_library(VibranceMediaRemoteAdapter SHARED ${_media_adapter_sources})
set_target_properties(VibranceMediaRemoteAdapter PROPERTIES
    FRAMEWORK TRUE FRAMEWORK_VERSION A
    MACOSX_FRAMEWORK_IDENTIFIER "com.vibrance.MediaRemoteAdapter"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    OBJC_VISIBILITY_PRESET default)
target_include_directories(VibranceMediaRemoteAdapter PRIVATE
    "${_media_adapter}/include" "${_media_adapter}/src")
target_compile_options(VibranceMediaRemoteAdapter PRIVATE -fobjc-arc)
target_link_libraries(VibranceMediaRemoteAdapter PRIVATE
    "-framework Foundation" "-framework AppKit" "-framework UniformTypeIdentifiers")
add_custom_command(TARGET VibranceMediaRemoteAdapter POST_BUILD
    COMMAND /usr/bin/codesign --force --sign -
        "$<TARGET_BUNDLE_DIR:VibranceMediaRemoteAdapter>" VERBATIM)
configure_file("${_media_adapter}/bin/mediaremote-adapter.pl"
    "${CMAKE_CURRENT_BINARY_DIR}/mediaremote-adapter.pl" COPYONLY)
add_dependencies(vibrance_engine VibranceMediaRemoteAdapter)
install(TARGETS VibranceMediaRemoteAdapter FRAMEWORK DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${_media_adapter}/bin/mediaremote-adapter.pl" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${_media_adapter}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine/licences"
    RENAME mediaremote-adapter.txt)
