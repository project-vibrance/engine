# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

include(CMakePackageConfigHelpers)
set(CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/vibrance_engine")

if(APPLE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/src/media/macos_adapter.cmake")
endif()

install(TARGETS vibrance_engine
    EXPORT vibrance_engineTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(VIBRANCE_BUNDLED_GLFW)
    set_property(TARGET vibrance_engine APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
    install(TARGETS glfw EXPORT vibrance_engineTargets
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES "${GLFW_PATH}/LICENSE.md"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine/licences"
        RENAME glfw.txt)
endif()
if(VIBRANCE_WIN32_COMPOSITION_AVAILABLE)
    install(FILES "${VIBRANCE_COMPOSITION_DLL}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/VibranceLockedRuntime.cmake")
    vibrance_install_locked_runtime("${VIBRANCE_LIQUID_DWM_EXTENSION}"
        "${CMAKE_INSTALL_BINDIR}")
    install(PROGRAMS "${VIBRANCE_LIQUID_DWM_BROKER}" "${VIBRANCE_LIQUID_DWM_OBSERVER}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
    if(VIBRANCE_LIQUID_DBGHELP_RUNTIME AND VIBRANCE_LIQUID_SYMSRV_RUNTIME)
        install(FILES "${VIBRANCE_LIQUID_DBGHELP_RUNTIME}"
            "${VIBRANCE_LIQUID_SYMSRV_RUNTIME}"
            DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
endif()
if(TARGET vibrance_liquid_glass_shader)
    install(FILES "${VIBRANCE_LIQUID_SHADER_OBJECT}"
        "${VIBRANCE_LIQUID_COMPARE_OBJECT}"
        "${VIBRANCE_LIQUID_UI_SHADER_OBJECT}"
        "${VIBRANCE_LIQUID_FULLSCREEN_OBJECT}"
        ${VIBRANCE_LIQUID_BLUR_OBJECTS}
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/licences/liquid-glass-winui.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licences/detours.txt"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine/licences")
if(VIBRANCE_WIN32_INTEROP_AVAILABLE)
    install(FILES "${VIBRANCE_WIN32_INTEROP_DLL}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()


install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/vibranceUI/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vibranceUI")

if(APPLE)
    install(FILES "${METAL_CPP_INCLUDE_DIR}/LICENSE.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine/licences"
        RENAME metal-cpp.txt)
endif()

install(FILES "${ENGINE_MANIFEST_JSON}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine")





file(REAL_PATH "${GLM_INCLUDE_DIR}/glm" _vibrance_glm_headers)
install(DIRECTORY "${_vibrance_glm_headers}"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${ENTT_INCLUDE_DIR}/entt"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(NOT APPLE)
install(FILES "${VMA_HEADER_TO_INSTALL}"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vma"
    RENAME vk_mem_alloc.h)
endif()

if(EXISTS "${GLFW_INCLUDE_DIRS}/GLFW")
    file(REAL_PATH "${GLFW_INCLUDE_DIRS}/GLFW" _vibrance_glfw_headers)
    install(DIRECTORY "${_vibrance_glfw_headers}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/GLFW")
endif()
if(NOT APPLE)
install(DIRECTORY "${Vulkan_INCLUDE_DIRS}/vulkan"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(EXISTS "${Vulkan_INCLUDE_DIRS}/vk_video")
    install(DIRECTORY "${Vulkan_INCLUDE_DIRS}/vk_video"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()

endif()

install(EXPORT vibrance_engineTargets
    FILE vibrance_engineTargets.cmake
    NAMESPACE vibrance::
    DESTINATION "${CONFIG_INSTALL_DIR}")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vibrance_engineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/vibrance_engineConfig.cmake"
    INSTALL_DESTINATION "${CONFIG_INSTALL_DIR}"
    PATH_VARS CMAKE_INSTALL_DATADIR)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vibrance_engineConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vibrance_engineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vibrance_engineConfigVersion.cmake"
    DESTINATION "${CONFIG_INSTALL_DIR}")


install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VibranceApplication.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VibranceLockedRuntime.cmake"
    DESTINATION "${CONFIG_INSTALL_DIR}")
install(PROGRAMS
    "${PROJECT_SOURCE_DIR}/scripts/create-vibrance-app.sh"
    "${PROJECT_SOURCE_DIR}/scripts/create-vibrance-app.bat"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine/tools")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/scripts/templates"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine/tools")

install(FILES "${PROJECT_SOURCE_DIR}/README.md" "${PROJECT_SOURCE_DIR}/LICENCE"
    "${PROJECT_SOURCE_DIR}/NOTICE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine/docs"
    FILES_MATCHING PATTERN "*.md")
if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/patches/vibrance-kwin-liquid-glass-6.8.patch"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine/patches")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/protocols/vibrance-liquid-glass-v1.xml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine/protocols")
endif()
