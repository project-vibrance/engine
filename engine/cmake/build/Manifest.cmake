# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

set(ENGINE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ENGINE_GENERATED_DIR}")
set(ENGINE_MANIFEST_CPP "${ENGINE_GENERATED_DIR}/engine_manifest.cpp")
set(ENGINE_MANIFEST_JSON "${ENGINE_GENERATED_DIR}/vibrance_engine_manifest.json")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/engine_manifest.cpp.in"
    "${ENGINE_MANIFEST_CPP}"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vibrance_engine_manifest.json.in"
    "${ENGINE_MANIFEST_JSON}"
    @ONLY)
list(APPEND ENGINE_SOURCES "${ENGINE_MANIFEST_CPP}")
