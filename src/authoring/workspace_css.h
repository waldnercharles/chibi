#ifndef CHIBI_WORKSPACE_CSS_H
#define CHIBI_WORKSPACE_CSS_H
#include "support/arena.h"
typedef struct ui_source_provider ui_source_provider;
typedef struct workspace_documents workspace_documents;
typedef struct
{
    const char *path, *file, *selector, *name, *value, *fingerprint;
    int start, end;
} workspace_css_row;
typedef struct
{
    workspace_documents *documents;
    ui_source_provider *provider;
} workspace_css_service;
/* File discovery follows one UI page's imports/widgets through the existing
 * bounded no-follow source provider. No document handles are allocated. */
bool workspace_css_query(
    ui_source_provider *,
    const char *page,
    arena *,
    workspace_css_row **rows,
    int *count,
    char *error,
    size_t
);
/* Resolves the explicit scene UI override, then project.kdl's ui. */
bool workspace_css_document_query(
    workspace_css_service *,
    int document,
    int revision,
    arena *,
    workspace_css_row **rows,
    int *count,
    char *error,
    size_t
);
/* Revisioned workspace edits own undo/history and conflict-aware disk saves. */
bool workspace_css_edit(
    workspace_documents *,
    const char *path,
    const char *fingerprint,
    int start,
    int end,
    const char *old_value,
    const char *value,
    int *document,
    int *revision,
    char *error,
    size_t
);
#endif
