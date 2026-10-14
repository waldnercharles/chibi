#ifndef CHIBI_DOCUMENT_SEMANTICS_H
#define CHIBI_DOCUMENT_SEMANTICS_H

#include "assets/ui_source.h"
#include "authoring/source_edit.h"
#include "authoring/source_intelligence.h"
#include "authoring/workspace_documents.h"
#include "execution/diagnostic.h"

typedef struct document_semantics document_semantics;
/* One immutable, revision-bound view of an owned document. The catalog is
 * borrowed and must outlive this view. No source is executed or registered. */
document_semantics *document_semantics_new(
    const workspace_documents *,
    int document,
    int revision,
    const script_catalog *,
    char *error,
    size_t error_size
);
/* Content is the validated workspace-relative content directory. Source
 * identities use paths relative to it, matching compiler module identities. */
document_semantics *document_semantics_new_content(
    const workspace_documents *,
    int document,
    int revision,
    const script_catalog *,
    const char *content,
    char *error,
    size_t error_size
);
void document_semantics_free(document_semantics *);
const script_catalog *document_semantics_catalog(const document_semantics *);
int document_semantics_revision(const document_semantics *);
bool document_semantics_valid(const document_semantics *);
int document_semantics_diagnostic_count(const document_semantics *);
const script_diagnostic *document_semantics_diagnostic(const document_semantics *, int index);
const authoring_source_presentation *document_semantics_presentation(const document_semantics *);
const behavior_asset *document_semantics_graph(const document_semantics *);
const compile_report *document_semantics_compile(const document_semantics *);
const char *document_semantics_dialect(const document_semantics *);
const script_analysis *document_semantics_analysis(const document_semantics *);

/* Borrowed index for definitions, references, call hierarchy and exact source
 * ranges. It retains useful resolved facts even when the draft is invalid. */
const authoring_navigation *document_semantics_navigation(const document_semantics *);
const ui_source *document_semantics_ui(const document_semantics *);
authoring_highlight *document_semantics_highlights(const document_semantics *, int *count);
void document_semantics_complete(
    const document_semantics *,
    int offset,
    const ui_source_project *,
    authoring_completions *
);
char *document_semantics_hover(const document_semantics *, int offset, const ui_source_project *);

/* All edits check both the document handle and exact revision. Validated edits
 * preserve the caller's existing transaction boundary; they never save disk. */
/* Optional owned layout metadata is bound to the resulting source bytes. */
bool document_semantics_prepare_edit(
    const document_semantics *,
    const authoring_edit_request *,
    authoring_edit_result *,
    char *,
    size_t
);
bool document_semantics_edit_metadata(
    workspace_documents *,
    const document_semantics *,
    const authoring_edit_request *,
    char **metadata,
    char *error,
    size_t error_size
);
bool document_semantics_edit(
    workspace_documents *,
    const document_semantics *,
    const authoring_edit_request *,
    char *error,
    size_t error_size
);
bool document_semantics_replace(
    workspace_documents *,
    const document_semantics *,
    int start,
    int end,
    const char *replacement,
    bool validate,
    char *error,
    size_t error_size
);
/* Service borrowed by native semantic capabilities. Catalog lifetimes are
 * explicit; host composition updates it only after outstanding calls finish. */
bool document_semantics_find(
    const workspace_documents *,
    int document,
    int revision,
    const char *query,
    int from,
    bool ignorecase,
    int *start,
    int *end,
    char *error,
    size_t size
);
bool document_semantics_rename(
    workspace_documents *,
    const document_semantics *,
    int offset,
    const char *name,
    char *error,
    size_t size
);
typedef bool (*document_semantics_catalog_acquire_fn)(
    void *,
    const script_catalog **catalog,
    void **owner,
    char *,
    size_t
);
typedef void (*document_semantics_catalog_release_fn)(void *, void *owner);
typedef struct
{
    workspace_documents *documents;
    const script_catalog *catalog;
    const ui_source_project *ui_project;
    document_semantics_catalog_acquire_fn catalog_acquire;
    document_semantics_catalog_release_fn catalog_release;
    void *catalog_user;
    const char *content_prefix; /* Borrowed session manifest path; default content. */
} document_semantics_service;
/* Owns an acquired catalog generation until snapshot free. */
document_semantics *document_semantics_service_snapshot(
    document_semantics_service *,
    int document,
    int revision,
    char *,
    size_t
);
#endif
