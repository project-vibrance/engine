cmake_minimum_required(VERSION 3.28)
project(hello_vibrance VERSION 0.1.0 LANGUAGES CXX)
find_package(vibrance_engine 0.1 CONFIG REQUIRED)
vibrance_add_application(hello_vibrance SOURCES src/main.cpp ASSETS assets)
