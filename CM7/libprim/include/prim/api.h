#pragma once
/**
 * @file api.h
 * @brief Visibility/export macro for the public libprim API.
 *
 * Only functions marked PRIM_API are exported when the library is built with
 * -fvisibility=hidden. Everything else is internal to the translation unit or
 * to the library (prim_internal_*) and cannot be linked from outside.
 */

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(PRIM_BUILDING)
    #define PRIM_API __declspec(dllexport)
  #else
    #define PRIM_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define PRIM_API __attribute__((visibility("default")))
#else
  #define PRIM_API
#endif

/**
 * @brief Marks a symbol that is shared across libprim translation units but is
 *        NOT part of the public client API (e.g. backend installation).
 *
 * Such symbols stay hidden from clients (no PRIM_API), yet are visible to the
 * other .c files of the library at link time because they are not static.
 */
#define PRIM_INTERNAL
