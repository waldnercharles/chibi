#ifndef CHIBI_AUTOMATION_UI_EDIT_H
#define CHIBI_AUTOMATION_UI_EDIT_H

#include <stddef.h>

typedef struct yyjson_mut_doc yyjson_mut_doc;
typedef struct yyjson_mut_val yyjson_mut_val;
typedef struct yyjson_val yyjson_val;

/* The MCP's ui asset tools over the ui language service
 * (assets/ui_source.h). None does IO but what reading the project's
 * widgets and stylesheets under `content_root` needs. */

/* validateAsset: the syntax errors tree-sitter finds and what
 * ui_document_read rejects, at file lines; then, when those are clean, every
 * warning RmlUi logs loading the asset with its widgets and imports
 * (render/ui_check.h). Appends each to `diagnostics` and returns how many
 * there are. */
int automation_ui_validate(
    const char *content_root,
    const char *name,
    const char *text,
    yyjson_mut_doc *doc,
    yyjson_mut_val *diagnostics
);

/* buildUi: applies the request's `operations` to the asset's file text
 * `source` in order, each to the text the one before left, and lists each
 * one's edits in `result`. Returns CHIBI_AUTOMATION_OK with the owned new
 * text in `edited`, or another status with a message in `error`. */
int automation_ui_edit(
    const char *source,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **edited,
    char *error,
    size_t error_size
);

/* inspectUi: the asset's outline, the props its bindings read, the events
 * it emits, its slots, the widgets it uses and the props it passes them, the
 * custom properties, classes and save keys it names, and its syntax
 * errors. */
void automation_ui_inspect(const char *text, yyjson_mut_doc *doc, yyjson_mut_val *result);

#endif
