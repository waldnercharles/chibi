#ifndef CHIBI_UI_DOCUMENT_H
#define CHIBI_UI_DOCUMENT_H

#include <stdbool.h>
#include <stddef.h>

/* A ui asset: RML body markup and the RCSS that styles it. */
typedef struct ui_document
{
    char *rml;
    char *rcss; /* empty when the asset has none */
} ui_document;

/* Parses a ui asset into `out`, which is empty or holds an earlier read.
 * Returns false with a message in `error` and leaves `out` unchanged when the
 * asset is invalid, so a caller keeps the last valid document. */
bool ui_document_read(const char *text, ui_document *out, char *error, size_t error_size);
/* Replaces each <use ui="Name" .../> in the markup with the widget asset
 * `ui_root`/Name.yaml: a div with the use element's attributes, `ui` renamed
 * data-ui, around the widget's markup. The use element's children fill the
 * widget's <slot/>. Each widget's RCSS precedes the document's, once. Appends
 * every widget name it looks up to `uses` (support/array.h), even on failure.
 * Returns false with a message in `error`, leaving `document` unchanged, when
 * a widget is missing, invalid, or uses itself. */
bool ui_document_expand(ui_document *document, const char *ui_root, char ***uses, char *error, size_t error_size);
/* Appends the name of each widget the markup uses directly to `names`. */
void ui_document_use_names(const char *rml, char ***names);
/* Owned RML document text: `base` then the asset's RCSS in the head, and its
 * markup in the body. */
char *ui_document_compose(const ui_document *document, const char *base);
void ui_document_free(ui_document *document);

#endif
