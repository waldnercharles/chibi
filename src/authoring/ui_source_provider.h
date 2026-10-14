#ifndef CHIBI_UI_SOURCE_PROVIDER_H
#define CHIBI_UI_SOURCE_PROVIDER_H
#include "authoring/workspace_documents.h"
#include "render/ui_loader.h"

#include <stdbool.h>
#include <stddef.h>
typedef struct ui_source_provider ui_source_provider;
/* Read-only source snapshots for an isolated render_ui. Root is a trusted host
 * workspace path, content a relative configured content folder. Documents are
 * borrowed and never opened/written. Caller clears every copied loader before
 * freeing the provider, after its live page/mount states have closed.
 * Each generation: <=128 requested files, <=1MiB/file, <=8MiB file/overlay bytes.
 * UTF-8 only; rooted reads refuse links, special/changing files and traversal. */
ui_source_provider *ui_source_provider_new(
    const char *root,
    const char *content,
    workspace_documents *,
    char *error,
    size_t
);
void ui_source_provider_free(ui_source_provider *);
/* Copies current UI document overlays, then invalidates the disk read cache.
 * Explicit refresh, never per-frame filesystem scanning. Failure retains the
 * complete previous generation. Refresh after a source revision or disk reload. */
bool ui_source_provider_refresh(ui_source_provider *, char *error, size_t);
render_ui_loader ui_source_provider_loader(ui_source_provider *);
/* Whether this generation actually read a named source in its UI closure. */
bool ui_source_provider_contains(const ui_source_provider *, const char *relative_path);
/* Cheap exact revision checks for open documents actually used by this closure.
 * Newly opened clean documents are byte-compared once. External disk changes
 * require explicit refresh; this function performs no filesystem IO. */
bool ui_source_provider_current(ui_source_provider *);
/* Visits imported styles, page styles, then widget styles. Path/text are
 * borrowed only through the callback; false stops a bounded inventory. */
typedef bool (*ui_source_styles_visit)(void *, const char *path, const char *text);
bool ui_source_provider_styles(
    ui_source_provider *,
    const char *page,
    ui_source_styles_visit,
    void *,
    char *error,
    size_t
);
#endif
