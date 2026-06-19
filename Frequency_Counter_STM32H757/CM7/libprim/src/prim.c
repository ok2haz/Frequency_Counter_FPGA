/**
 * @file prim.c
 * @brief Library-wide version and last-error reporting.
 */

#include <prim/prim.h>

#ifndef PRIM_VERSION_STRING
#define PRIM_VERSION_STRING "0.1.0"
#endif

static const char *g_last_error = "";

const char *prim_version(void) { return PRIM_VERSION_STRING; }

const char *prim_last_error(void) { return g_last_error; }

/* Internal setter, not exported as API. */
PRIM_INTERNAL void prim_internal_set_error(const char *msg);
PRIM_INTERNAL void prim_internal_set_error(const char *msg)
{
    g_last_error = (msg != 0) ? msg : "";
}
