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
/* Owned RML document text: `base` then the asset's RCSS in the head, and its
 * markup in the body. */
char *ui_document_compose(const ui_document *document, const char *base);
void ui_document_free(ui_document *document);

#endif
