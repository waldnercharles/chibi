#ifndef CHIBI_AUTOMATION_CONTROLLER_H
#define CHIBI_AUTOMATION_CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct game_prototype_preview game_prototype_preview;
typedef struct chibi_preview_run chibi_preview_run;
/* Tooling scene preparation shared by headless scenarios and GPU previews.
 * Returns an owned running preview; the caller must initialize rendering first. */
int chibi_automation_preview(
    const char *root,
    const char *request,
    chibi_preview_run **run,
    bool *perspective,
    char *error,
    size_t error_size
);
game_prototype_preview *chibi_preview_runtime(chibi_preview_run *run);
int chibi_preview_step(chibi_preview_run *run, char *error, size_t size);
/* Owned inspection JSON, taken outside the measured frame interval. */
char *chibi_preview_state(chibi_preview_run *run);
void chibi_preview_free(chibi_preview_run *run);

enum
{
    CHIBI_AUTOMATION_CONTRACT = 1,
    CHIBI_AUTOMATION_OK = 0,
    CHIBI_AUTOMATION_INVALID_REQUEST = 2,
    CHIBI_AUTOMATION_IO_ERROR = 3,
    CHIBI_AUTOMATION_VALIDATION_ERROR = 4,
    CHIBI_AUTOMATION_EXECUTION_ERROR = 5,
    CHIBI_AUTOMATION_NOT_FOUND = 6,
    CHIBI_AUTOMATION_CONFLICT = 7,
};

/* The short controller API is also used by the native regression suite. Keep
 * it independent from the line-oriented executable so callers can exercise
 * the public JSON seam without spawning a process. */
typedef struct
{
    const char *root;
} chibi_ctl_context;

enum
{
    CHIBI_CTL_OK = CHIBI_AUTOMATION_OK,
    CHIBI_CTL_INVALID_REQUEST = CHIBI_AUTOMATION_INVALID_REQUEST,
    CHIBI_CTL_NOT_FOUND = CHIBI_AUTOMATION_NOT_FOUND,
};

/* Handle one JSON request and return one JSON response owned by the caller.
 * `default_root` is used when the request omits project_root. The response is
 * safe to print to stdout; diagnostics are data, never process logging. */
char *chibi_automation_handle(const char *request, const char *default_root, int *exit_code);

/* Compatibility entry point for in-process editor and test callers. It uses
 * the same contract as the executable while preserving request IDs and the
 * method-shaped vocabulary used by the first controller prototype. */
char *chibi_ctl_handle(const chibi_ctl_context *context, const char *request, int *exit_code);

#endif
