#ifndef CHIBI_WORKSPACE_IMPORTS_H
#define CHIBI_WORKSPACE_IMPORTS_H
#include "assets/model_import_recipe.h"

#include <stdbool.h>
#include <stddef.h>
typedef struct workspace_imports workspace_imports;
typedef struct
{
    int plan;
    bool committed;
    const model_import_report *report;
    const model_import_action *actions;
    int action_count;
    const char *error;
} workspace_import_view;
/* Four retained read-only plans, aggregate source+output bytes<=64MiB,
 * <=4096actions and<=256warnings per plan. SDK validates current snapshots
 * again at apply. An absent service refuses prepare/read/apply/close without IO;
 * read clears its output and reports an unavailable error. No UI selection, naming, warning or retry policy lives here. */
workspace_imports *workspace_imports_new(const char *root);
void workspace_imports_free(workspace_imports *);
int workspace_import_prepare(
    workspace_imports *,
    const char *source,
    const char *name,
    bool reimport,
    bool strict,
    char *error,
    size_t size
);
bool workspace_import_read(workspace_imports *, int plan, workspace_import_view *);
bool workspace_import_apply(workspace_imports *, int plan, char *error, size_t size);
bool workspace_import_close(workspace_imports *, int plan);
#endif
