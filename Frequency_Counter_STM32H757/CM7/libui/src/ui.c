/**
 * @file ui.c
 * @brief libui version reporting.
 */

#include <ui/ui.h>

#ifndef UI_VERSION_STRING
#define UI_VERSION_STRING "0.1.0"
#endif

const char *ui_version(void) { return UI_VERSION_STRING; }
