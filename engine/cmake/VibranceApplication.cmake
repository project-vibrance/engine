# A complete executable, runtime staging and installation for an SDK consumer.
function(vibrance_add_application target_name)
    cmake_parse_arguments(PARSE_ARGV 1 APP "" "ASSETS" "SOURCES")
    if(APP_UNPARSED_ARGUMENTS OR APP_KEYWORDS_MISSING_VALUES OR NOT APP_SOURCES)
        message(FATAL_ERROR
            "Usage: vibrance_add_application(name SOURCES source.cpp... [ASSETS directory])")
    endif()
    add_executable(${target_name} ${APP_SOURCES})
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    set_target_properties(${target_name} PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(${target_name} PRIVATE vibrance::engine)
    vibrance_engine_copy_runtime_companions(${target_name})

    if(WIN32 AND MINGW)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_link_options(${target_name} PRIVATE -static -static-libgcc -static-libstdc++)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_link_options(${target_name} PRIVATE -static)
        endif()
    elseif(APPLE)
        set_target_properties(${target_name} PROPERTIES
            BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_target_properties(${target_name} PROPERTIES
            BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    endif()

    include(GNUInstallDirs)
    install(TARGETS ${target_name} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(IMPORTED_RUNTIME_ARTIFACTS vibrance::engine
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}")
    foreach(companion IN LISTS vibrance_engine_RUNTIME_COMPANIONS)
        install(FILES "${companion}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endforeach()
    if(APP_ASSETS)
        get_filename_component(assets "${APP_ASSETS}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NOT IS_DIRECTORY "${assets}")
            message(FATAL_ERROR "Application assets directory does not exist: ${assets}")
        endif()
        add_custom_command(TARGET "${target_name}_vibrance_runtime_companions" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${assets}" "$<TARGET_FILE_DIR:${target_name}>/assets"
            VERBATIM)
        install(DIRECTORY "${assets}/" DESTINATION "${CMAKE_INSTALL_BINDIR}/assets")
    endif()
endfunction()
