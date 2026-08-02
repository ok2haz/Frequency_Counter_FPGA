#pragma once
/**
 * @file ui.h
 * @brief libui umbrella header — clients include only this.
 *
 * libui is the project's visual vocabulary: palette, dimensions, components,
 * charts and vector icons. Reusable in any project with the same look & feel.
 * Depends only on libprim.
 *
 * @code
 * #include <ui/ui.h>
 * @endcode
 */

#include <ui/api.h>
#include <ui/theme.h>
#include <ui/dimensions.h>
#include <ui/fonts.h>
#include <ui/pill.h>
#include <ui/card.h>
#include <ui/button.h>
#include <ui/segmented.h>
#include <ui/sparkline.h>
#include <ui/digit_group.h>
#include <ui/big_number.h>
#include <ui/bargraph.h>
#include <ui/icons.h>

/** @brief libui semantic version string. */
UI_API const char *ui_version(void);
