#include "authoring/workspace_imports.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum
{
    PLAN_MAX = 4,
    RETAINED_MAX = 64 * 1024 * 1024
};
typedef struct
{
    int handle;
    model_import_recipe_plan *recipe;
    size_t bytes;
    char error[256];
} plan;
struct workspace_imports
{
    char *root;
    int next;
    size_t retained;
    plan plans[PLAN_MAX];
};
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static plan *find(workspace_imports *s, int handle)
{
    if (!s) {
        return NULL;
    }
    for (int i = 0; i < PLAN_MAX; i++) {
        if (handle > 0 && s->plans[i].handle == handle) {
            return &s->plans[i];
        }
    }
    return NULL;
}
workspace_imports *workspace_imports_new(const char *root)
{
    if (!root || !*root) {
        return NULL;
    }
    workspace_imports *s = xcalloc(1, sizeof *s);
    s->root = xstrdup(root);
    s->next = 1;
    return s;
}
int workspace_import_prepare(
    workspace_imports *s,
    const char *source,
    const char *name,
    bool reimport,
    bool strict,
    char *error,
    size_t size
)
{
    if (!s) {
        fail(error, size, "Workspace imports are unavailable");
        return 0;
    }
    if (!source || !name || s->next == INT_MAX) {
        fail(error, size, "Import requires explicit source and name");
        return 0;
    }
    plan *slot = NULL;
    for (int i = 0; i < PLAN_MAX; i++) {
        if (!s->plans[i].handle) {
            slot = &s->plans[i];
            break;
        }
    }
    if (!slot) {
        fail(error, size, "Close an import plan before preparing another; maximum four retained plans");
        return 0;
    }
    model_import_recipe_plan *recipe = NULL;
    if (!model_import_recipe_prepare(s->root, *source ? source : NULL, name, reimport, strict, &recipe, error, size)) {
        return 0;
    }
    const model_import_report *report = model_import_recipe_report(recipe);
    int count;
    model_import_recipe_actions(recipe, &count);
    size_t bytes = report->source_bytes + report->output_bytes;
    if (bytes < report->source_bytes || bytes > RETAINED_MAX - s->retained ||
        count > 4096 || acount(report->warnings) > 256) {
        model_import_recipe_free(recipe);
        fail(error, size, "Import exceeds the retained 64MiB,4096output or256warning limits");
        return 0;
    }
    *slot = (plan){ .handle = s->next++, .recipe = recipe, .bytes = bytes };
    s->retained += bytes;
    return slot->handle;
}
bool workspace_import_read(workspace_imports *s, int handle, workspace_import_view *out)
{
    *out = (workspace_import_view){
        .error = s ? "Import plan is closed" : "Workspace imports are unavailable"
    };
    plan *p = find(s, handle);
    if (!p) {
        return false;
    }
    *out = (workspace_import_view){ .plan = handle,
                                    .committed = model_import_recipe_committed(p->recipe),
                                    .report = model_import_recipe_report(p->recipe),
                                    .error = p->error };
    out->actions = model_import_recipe_actions(p->recipe, &out->action_count);
    return true;
}
bool workspace_import_apply(workspace_imports *s, int handle, char *error, size_t size)
{
    if (!s) {
        return fail(error, size, "Workspace imports are unavailable");
    }
    plan *p = find(s, handle);
    if (!p) {
        return fail(error, size, "Import plan is closed");
    }
    if (model_import_recipe_committed(p->recipe)) {
        return fail(error, size, "Import was already published; do not retry this plan");
    }
    bool ok = model_import_recipe_apply(p->recipe, p->error, sizeof p->error);
    if (size) {
        snprintf(error, size, "%s", p->error);
    }
    return ok;
}
bool workspace_import_close(workspace_imports *s, int handle)
{
    plan *p = find(s, handle);
    if (!p) {
        return false;
    }
    s->retained -= p->bytes;
    model_import_recipe_free(p->recipe);
    *p = (plan){ 0 };
    return true;
}
void workspace_imports_free(workspace_imports *s)
{
    if (!s) {
        return;
    }
    for (int i = 0; i < PLAN_MAX; i++) {
        if (s->plans[i].handle) {
            workspace_import_close(s, s->plans[i].handle);
        }
    }
    free(s->root);
    free(s);
}
