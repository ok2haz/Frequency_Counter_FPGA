#pragma once
/**
 * @file prim.h
 * @brief libprim umbrella header — clients include only this.
 *
 * libprim is a generic, stateless 2D renderer reusable in any embedded project
 * with an RGB565 framebuffer. It knows nothing about palettes, UI components or
 * the frequency counter. It depends only on a HAL abstraction (display + an
 * optional DMA2D engine installed via dependency injection).
 *
 * @code
 * #include <prim/prim.h>
 * @endcode
 */

#include <prim/api.h>
#include <prim/types.h>
#include <prim/fb.h>
#include <prim/accel.h>
#include <prim/fill.h>
#include <prim/shapes.h>
#include <prim/path.h>
#include <prim/gradient.h>
#include <prim/glow.h>
#include <prim/text.h>

/** @brief Library semantic version string, e.g. "0.1.0". */

/** @brief Last error message for the current context (never NULL). */
