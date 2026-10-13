#ifndef CHIBI_MODEL_IMPORT_RECIPE_H
#define CHIBI_MODEL_IMPORT_RECIPE_H
#include "assets/model_import.h"

typedef struct model_import_recipe_plan model_import_recipe_plan;
typedef enum
{
    MODEL_IMPORT_CREATE,
    MODEL_IMPORT_UPDATE,
    MODEL_IMPORT_REMOVE,
    MODEL_IMPORT_PRESERVE
} model_import_action_kind;
typedef struct
{
    const char *key;
    model_import_action_kind action;
    /* PRESERVE means all source changes to this authored output are deferred. */
} model_import_action;
/* Source is project-relative (absolute paths inside project also accepted).
 * Reimport reads content/imports/<name>.kdl; source may be NULL in that mode.
 * Recipe mode requires project-contained sources. Strict rejects any warning.
 * Both operations are read-only until apply, including on conflicts. */
bool model_import_recipe_prepare(
    const char *project,
    const char *source,
    const char *name,
    bool reimport,
    bool strict,
    model_import_recipe_plan **result,
    char *error,
    size_t size
);
const model_import_plan *model_import_recipe_import_plan(const model_import_recipe_plan *);
const model_import_report *model_import_recipe_report(const model_import_recipe_plan *);
const model_import_action *model_import_recipe_actions(const model_import_recipe_plan *, int *count);
/* Validates source, output, recipe and declaration snapshots again. Stages all
 * bytes, then publishes with rollback. A committed plan cannot be reused, even
 * if cleanup fails; that error says "Import published" and names residual
 * paths. Failed rollback reports recoverable originals before other residual
 * files. This is not crash-atomic; callers must coordinate concurrent writers.
 * Failures set errno: EINVAL for invalid input, EEXIST for conflicts, ESTALE
 * for changed snapshots, EIO for publication/read failures. */
bool model_import_recipe_apply(model_import_recipe_plan *, char *error, size_t size);
/* Every output and recipe was published, even if later cleanup reported
 * failure. A false apply result with committed=true must not be retried. */
bool model_import_recipe_committed(const model_import_recipe_plan *);
void model_import_recipe_free(model_import_recipe_plan *);
#endif
