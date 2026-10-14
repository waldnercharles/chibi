#ifndef CHIBI_PROJECT_SEMANTICS_H
#define CHIBI_PROJECT_SEMANTICS_H
#include "assets/prototype.h"
#include "authoring/source_catalog.h"
#include "authoring/source_navigation.h"
#include "authoring/workspace_documents.h"
#include "support/arena.h"

typedef struct project_semantics project_semantics;
typedef struct
{
    const char *path, *message;
    int start, end, line, column;
} project_diagnostic;
bool project_semantics_diagnostics(
    project_semantics *,
    int first,
    int count,
    arena *,
    project_diagnostic **,
    int *row_count,
    int *total,
    char *,
    size_t
);
typedef struct
{
    const char *path, *label, *signature, *target;
    int start, end, target_start, target_end;
} document_call;
typedef struct
{
    const char *path, *name;
    int start, end, kind;
} project_reference;
/* Root and native declarations are copied/borrowed explicitly by the service.
 * Optional project type callback is synchronous and borrowed during snapshot
 * creation only; it must not publish process-global catalogs. */
typedef const script_catalog *(*project_semantics_catalog_fn)(void *, char *, size_t);
project_semantics *project_semantics_new(workspace_documents *, const char *root, const script_catalog *native);
project_semantics *project_semantics_new_content(
    workspace_documents *,
    const char *root,
    const char *content,
    const script_catalog *native
);
void project_semantics_free(project_semantics *);
/* Cached immutable project generation; refreshed by open handle/revision or
 * component snapshot changes. Explicit refresh detects external disk edits.
 * Acquired generations outlive replacement and service destruction. */
bool project_semantics_catalog_acquire(void *, const script_catalog **, void **owner, char *, size_t);
void project_semantics_catalog_release(void *, void *owner);
void project_semantics_refresh(project_semantics *);
typedef const script_catalog *(*project_semantics_candidate_fn)(
    void *,
    const workspace_document_replacement *,
    int count,
    void **owner,
    char *error,
    size_t size
);
typedef void (*project_semantics_candidate_release_fn)(void *, void *owner);
/* Candidate callback owns unpublished registry state until matching release;
 * custom schema refactors require it instead of reusing current declarations. */
void project_semantics_set_candidate_catalog(
    project_semantics *,
    project_semantics_candidate_fn,
    project_semantics_candidate_release_fn,
    void *
);
void project_semantics_set_catalog(project_semantics *, project_semantics_catalog_fn, void *);
/* Borrowed only during snapshot construction. NULL candidate_owner selects
 * current scope; a candidate owner must select that exact private generation. */
typedef const prototype_registry
    *(*project_semantics_registry_fn)(void *, void *candidate_owner, char *, size_t);
void project_semantics_set_registry(project_semantics *, project_semantics_registry_fn, void *);

bool project_semantics_calls(
    project_semantics *,
    int document,
    int revision,
    int offset,
    int direction,
    int first,
    int count,
    arena *,
    document_call **rows,
    int *row_count,
    int *total,
    char *,
    size_t
);
bool project_semantics_references(
    project_semantics *,
    int document,
    int revision,
    int offset,
    int first,
    int count,
    arena *,
    project_reference **rows,
    int *row_count,
    int *total,
    char *,
    size_t
);
/* Complete candidate semantic validation precedes atomic in-memory adoption.
 * Newly opened changed files are closed again on refusal. Files never save. */
bool project_semantics_rename(
    project_semantics *,
    int document,
    int revision,
    int offset,
    const char *name,
    int *changed,
    int *changed_count,
    char *,
    size_t
);
#endif
