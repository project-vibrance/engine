#pragma once

#if defined(_WIN32) || defined(_WIN64)
  #if defined(VIBRANCE_ENGINE_EXPORTS)
    #define VIBRANCE_ENGINE_API __declspec(dllexport)
  #else
    #define VIBRANCE_ENGINE_API __declspec(dllimport)
  #endif
#else
  #if __GNUC__ >= 4
    #define VIBRANCE_ENGINE_API __attribute__((visibility("default")))
  #else
    #define VIBRANCE_ENGINE_API
  #endif
#endif

#define VIBRANCE_GLFW_API VIBRANCE_ENGINE_API
