#ifndef CHIBI_UI_DOCUMENT_H
#define CHIBI_UI_DOCUMENT_H

#include <stdbool.h>
#include <stddef.h>

/* A ui asset: RML body markup and the RCSS that styles it. A scene's root
 * document may wrap its markup in <body ...>, as a web page's layout does,
 * to set the page body's attributes, such as a data-class-dark binding. */
typedef struct ui_document
{
    char *rml;
    char *rcss; /* empty when the asset has none */
    char *body; /* the <body> attributes as written, or NULL */
} ui_document;

/* Parses a ui asset into `out`, which is empty or holds an earlier read.
 * RCSS @imports resolve in `directory`, appending each file read to
 * `imports` (support/array.h) when it is not NULL; a NULL directory leaves
 * them as written. Returns false with a message in `error` and leaves `out`
 * unchanged when the asset is invalid, so a caller keeps the last valid
 * document. */
bool ui_document_read(
    const char *text,
    const char *directory,
    char ***imports,
    ui_document *out,
    char *error,
    size_t error_size
);
/* Replaces each <use ui="Name" .../> in the markup with the widget asset
 * `ui_root`/Name.yaml: a div with the use element's attributes, `ui` renamed
 * data-ui, around the widget's markup. A widget element with an attrs
 * attribute takes the use element's other attributes in its place instead,
 * as Vue's v-bind="$attrs" does, so a binding reaches the widget's input.
 * The use element's children fill the widget's <slot/>. The RCSS becomes each widget's, then the document's, each
 * scoped to its data-ui name (ui_document_scope), the document's to `name`.
 * Appends every widget name it looks up to `uses` (support/array.h), even on
 * failure. Returns false with a message in `error`, leaving `document`
 * unchanged, when a widget is missing, invalid, or uses itself. */
bool ui_document_expand(
    ui_document *document,
    const char *name,
    const char *ui_root,
    char ***uses,
    char *error,
    size_t error_size
);
/* Owned RCSS whose rules only match inside elements with data-ui="name":
 * `body` in a selector names that element, and other selectors match within
 * it. @media blocks are scoped too; other at-rules are kept as written. */
char *ui_document_scope(const char *rcss, const char *name);
/* Appends the name of each widget the markup uses directly to `names`. */
void ui_document_use_names(const char *rml, char ***names);
/* Owned RML document text: `base` then the asset's RCSS in the head, and its
 * markup in a body with its body attributes. */
char *ui_document_compose(const ui_document *document, const char *base);
void ui_document_free(ui_document *document);

#endif
