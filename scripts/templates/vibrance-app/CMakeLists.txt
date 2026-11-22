# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)
project(hello_vibrance VERSION 0.1.0 LANGUAGES CXX)
find_package(vibrance_engine 0.7 CONFIG REQUIRED)
vibrance_add_application(hello_vibrance SOURCES src/main.cpp src/starter.cpp ASSETS assets)
include(GNUInstallDirs)
install(FILES README.md LICENCE DESTINATION "${CMAKE_INSTALL_DATADIR}/hello_vibrance")
