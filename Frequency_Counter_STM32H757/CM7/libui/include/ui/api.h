#pragma once
/**
 * @file api.h
 * @brief Visibility/export macro for the public libui API.
 */

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(UI_BUILDING)
    #define UI_API __declspec(dllexport)
  #else
    #define UI_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define UI_API __attribute__((visibility("default")))
#else
  #define UI_API
#endif

#define UI_INTERNAL
