#ifndef CHIBI_UI_SOURCE_PREVIEW_H
#define CHIBI_UI_SOURCE_PREVIEW_H
#include "authoring/workspace_documents.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct workspace_runtime workspace_runtime;
typedef struct ui_dom ui_dom;
typedef struct ui_source_preview ui_source_preview;
typedef struct
{
    const char *root, *content;
    workspace_documents *documents;
    workspace_runtime *runtime;
    ui_dom *dom;
} ui_source_preview_config;
typedef struct
{
    bool ok, stale;
    int preview, document, revision, session, width, height;
    uint64_t texture, texture_revision;
    const char *context, *error;
} ui_source_preview_result;
/* One retained <=2048x2048 canvas. Services are borrowed and outlive this host;
 * free before graphics shutdown. Source changes parse UI only, never compile
 * or execute behavior source. Source/model selection and gestures are caller
 * policy. Session0 uses an empty model/resident resource context. */
ui_source_preview *ui_source_preview_new(const ui_source_preview_config *, char *error, size_t);
void ui_source_preview_free(ui_source_preview *);
ui_source_preview_result ui_source_preview_open(
    ui_source_preview *,
    int document,
    int revision,
    const char *asset,
    int session,
    uint64_t model_entity
);
ui_source_preview_result ui_source_preview_draw(
    ui_source_preview *,
    int preview,
    int revision,
    int width,
    int height,
    double seconds
);
ui_source_preview_result ui_source_preview_close(ui_source_preview *, int preview);
/* Host calls before target UI/world retirement; borrowed model/resource pointers
 * never survive it. The next authored draw reports missing/stale preview. */
void ui_source_preview_retire_target(ui_source_preview *);
ui_source_preview_result ui_source_preview_current(const ui_source_preview *);
#endif
