#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Explicit capabilities, never inferred from a hostname or graphics driver. */
static inline const char *chibi_execution_profile(void)
{
    const char *value = getenv("CHIBI_EXECUTION_PROFILE");
    return value ? value : "iteration";
}

static inline bool chibi_execution_is(const char *name)
{
    return !strcmp(chibi_execution_profile(), name);
}

static inline bool chibi_execution_hardware(void)
{
    const char *allow = getenv("CHIBI_ALLOW_FULL_GPU");
    return chibi_execution_is("hardware") && allow && !strcmp(allow, "1");
}
