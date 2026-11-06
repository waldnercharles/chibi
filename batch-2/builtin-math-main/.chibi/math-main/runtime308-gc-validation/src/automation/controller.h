#ifndef CHIBI_AUTOMATION_CONTROLLER_H
#define CHIBI_AUTOMATION_CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct prototype_preview prototype_preview;
typedef struct chibi_preview_run chibi_preview_run;
/* Tooling scene preparation shared by headless scenarios and GPU previews.
 * Returns an owned running preview. Rendering hosts initialize rendering first;
 * headless callers can prepare, step and inspect without a renderer. */
int chibi_automation_preview(
    const char *root,
    const char *request,
    chibi_preview_run **run,
    char *error,
    size_t error_size
);
prototype_preview *chibi_preview_runtime(chibi_preview_run *run);
/* The root ui document the previewed UI shows on: the current scene's, else
 * the project's default; NULL for none. */
const char *chibi_preview_page(const chibi_preview_run *run);
bool chibi_preview_perspective(const chibi_preview_run *run);
/* Counts the scenes scripts have loaded, so a host can tell when to re-read
 * the page and projection and reset its camera. */
int chibi_preview_loads(const chibi_preview_run *run);
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

/* Handle one JSON request and return one JSON response owned by the caller.
 * `default_root` is used when the request omits project_root. The response is
 * safe to print to stdout; diagnostics are data, never process logging. */
char *chibi_automation_handle(const char *request, const char *default_root, int *exit_code);

/* The folder the project at `root` keeps its content in, which virtual
 * content/ paths name: the one its manifest names, joined to `root`. `root`
 * must be the project the current request or preview opened. */
const char *chibi_automation_content(const char *root);

/* Rewrites every authored asset under root in the canonical form its loader
 * and saver produce. With check, only reports differences. Returns the
 * number of differing assets, or -1 when an asset cannot be loaded or written. */
int chibi_automation_format_assets(const char *root, bool check);

#endif
