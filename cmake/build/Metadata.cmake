# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

set(VIBRANCE_ENGINE_NAME "vibranceUI")
set(VIBRANCE_ENGINE_RELEASE_STAGE "beta" CACHE STRING
    "Human-facing engine build stage: alpha, beta, release-candidate, or release" FORCE)
set_property(CACHE VIBRANCE_ENGINE_RELEASE_STAGE PROPERTY STRINGS
    alpha beta release-candidate release)
set(VIBRANCE_ENGINE_BUILD_SEQUENCE 0 CACHE STRING
    "Human-facing engine build sequence for the current UTC build date" FORCE)
if(NOT VIBRANCE_ENGINE_BUILD_SEQUENCE MATCHES "^[0-9]+$")
    message(FATAL_ERROR
        "VIBRANCE_ENGINE_BUILD_SEQUENCE must be a non-negative integer.")
endif()
if(VIBRANCE_ENGINE_RELEASE_STAGE STREQUAL "alpha")
    set(VIBRANCE_ENGINE_RELEASE_STAGE_SUFFIX "a")
elseif(VIBRANCE_ENGINE_RELEASE_STAGE STREQUAL "beta")
    set(VIBRANCE_ENGINE_RELEASE_STAGE_SUFFIX "b")
elseif(VIBRANCE_ENGINE_RELEASE_STAGE STREQUAL "release-candidate")
    set(VIBRANCE_ENGINE_RELEASE_STAGE_SUFFIX "rc")
elseif(VIBRANCE_ENGINE_RELEASE_STAGE STREQUAL "release")
    set(VIBRANCE_ENGINE_RELEASE_STAGE_SUFFIX "r")
else()
    message(FATAL_ERROR
        "VIBRANCE_ENGINE_RELEASE_STAGE must be alpha, beta, release-candidate, or release.")
endif()
string(TIMESTAMP VIBRANCE_ENGINE_BUILD_DATE "%Y%m%d" UTC)
string(TIMESTAMP VIBRANCE_ENGINE_LAST_UPDATED "%Y-%m-%d" UTC)
set(VIBRANCE_SUFFIX
    "${VIBRANCE_ENGINE_BUILD_DATE}.${VIBRANCE_ENGINE_BUILD_SEQUENCE}-${VIBRANCE_ENGINE_RELEASE_STAGE_SUFFIX}")
set(VIBRANCE_FULL_VERSION "${PROJECT_VERSION}-${VIBRANCE_SUFFIX}")
message(STATUS "vibranceUI display build: ${VIBRANCE_FULL_VERSION}")
