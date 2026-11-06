#ifndef CHIBI_REFACTOR_H
#define CHIBI_REFACTOR_H

#include "assets/prototype.h"
#include "authoring/reference_index.h"

#include <stdbool.h>
#include <stddef.h>

/* Rename and safe delete across a project's assets, as a language server's
 * do. The caller indexes the project (reference_index.h) and gives each
 * indexed asset's code source, parallel to the index's assets, or NULL when
 * it has none, as for an unresolved or unsaved asset. A plan holds the new
 * file text of every asset it changes, preserving unchanged source.
 * The caller publishes every edit
 * of a plan or none; planning never changes the texts. */
typedef struct
{
    const reference_index *index;
    const char *const *texts;
    const prototype_registry *registry; /* loads prototypes and systems */
} refactor_project;

typedef struct
{
    int asset;  /* its position in the index */
    char *text; /* its new file text; NULL when the plan deletes it */
    char *name; /* its new name when the plan renames it; NULL otherwise */
    /* The definitions and references it rewrites or removes, borrowed from
     * the index (support/array.h). */
    const reference **references;
} refactor_edit;

/* A value equal to the old name that no indexed reference accounts for,
 * left as it is: the asset holding it and the value's path in the document,
 * as graphs/0/nodes/3/inputs/Prototype/default. Such a value, like a name a
 * library call passes on to Spawn Prototype, may still mean the symbol. */
typedef struct
{
    int asset;
    char *path;
} refactor_mention;

typedef struct
{
    refactor_edit *edits;       /* support/array.h, by asset position */
    refactor_mention *mentions; /* for assets and events */
    const reference **dangling; /* the uses a forced delete leaves */
} refactor_plan;

/* Plans renaming a symbol to `name`: its definition and every reference to
 * it. An asset's file and its name follow; an event with a definition
 * renames that asset, and a variable of an event definition renames the
 * payload pins of the event's senders and handlers. A library function's or
 * parameter's calls in other assets follow. Returns 0, or -1 with the reason
 * in `error` and an empty plan when the name is invalid or taken, the symbol
 * is one authoring cannot rename (host events, components and fields, whose
 * keys come from C reflection, and scenes, which the project file names), or
 * a reference sits where the rename cannot rewrite it. */
int refactor_rename(
    const refactor_project *project,
    reference_symbol symbol,
    const char *name,
    refactor_plan *plan,
    char *error,
    size_t error_size
);

/* Appends the references that keep a symbol from being deleted: its uses,
 * less a function's calls to itself; a variable of an event definition is
 * used by the event's senders and handlers. */
void refactor_blockers(const reference_index *index, reference_symbol symbol, const reference ***uses);

/* Plans deleting a variable, function, macro, event definition or asset (not
 * a scene). While blockers remain it is refused with the first named in
 * `error`, unless `force`, which lists them as dangling. A variable goes
 * with its Get and Set nodes and bindings, as in Unreal, so its uses in its
 * own asset never dangle. */
int refactor_delete(
    const refactor_project *project,
    reference_symbol symbol,
    bool force,
    refactor_plan *plan,
    char *error,
    size_t error_size
);

/* A reference's place, for messages and previews: "Asset", "Asset > Graph",
 * "Scene > Object" or "Prototype > Ui.document". */
void refactor_describe(const reference_index *index, const reference *r, char *out, size_t size);

void refactor_plan_free(refactor_plan *plan);

#endif
