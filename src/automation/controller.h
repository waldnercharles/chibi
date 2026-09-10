#ifndef CBLUEPRINT_AUTOMATION_CONTROLLER_H
#define CBLUEPRINT_AUTOMATION_CONTROLLER_H

#include <stddef.h>

enum
{
    BLUEPRINTS_AUTOMATION_CONTRACT = 1,
    BLUEPRINTS_AUTOMATION_OK = 0,
    BLUEPRINTS_AUTOMATION_INVALID_REQUEST = 2,
    BLUEPRINTS_AUTOMATION_IO_ERROR = 3,
    BLUEPRINTS_AUTOMATION_VALIDATION_ERROR = 4,
    BLUEPRINTS_AUTOMATION_EXECUTION_ERROR = 5,
    BLUEPRINTS_AUTOMATION_NOT_FOUND = 6,
};

/* The short controller API is also used by the native regression suite. Keep
 * it independent from the line-oriented executable so callers can exercise
 * the public JSON seam without spawning a process. */
typedef struct
{
    const char *root;
} blueprints_ctl_context;

enum
{
    BLUEPRINTS_CTL_OK = BLUEPRINTS_AUTOMATION_OK,
    BLUEPRINTS_CTL_INVALID_REQUEST = BLUEPRINTS_AUTOMATION_INVALID_REQUEST,
    BLUEPRINTS_CTL_NOT_FOUND = BLUEPRINTS_AUTOMATION_NOT_FOUND,
};

/* Handle one JSON request and return one JSON response owned by the caller.
 * `default_root` is used when the request omits project_root. The response is
 * safe to print to stdout; diagnostics are data, never process logging. */
char *blueprints_automation_handle(const char *request, const char *default_root, int *exit_code);

/* Compatibility entry point for in-process editor and test callers. It uses
 * the same contract as the executable while preserving request IDs and the
 * method-shaped vocabulary used by the first controller prototype. */
char *blueprints_ctl_handle(const blueprints_ctl_context *context, const char *request, int *exit_code);

#endif
