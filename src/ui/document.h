#pragma once
#include "support/rcss_map.h"

#include <stddef.h>
/* A ui asset: RML body markup and the RCSS that styles it. A scene's root
 * document may wrap its markup in <body ...>, as a web page's layout does,
 * to set the page body's attributes, such as a data-class-dark binding. */
typedef struct ui_document
{
    char *rml;
    char *rcss;   /* empty when the asset has none */
    char *body;   /* the <body> attributes as written, or NULL */
    int rml_line; /* the line of the .rml file `rml` starts on */
    size_t rml_start,
        body_start; /* original UTF-8 byte offsets before body splitting */
    int body_line;
    rcss_map map; /* where each rcss line came from; the .rcss file's own are -1 */
} ui_document;

/* Expanded markup only; composition does not parse authored assets. */
char *ui_document_compose(const ui_document *, const char *base);
void ui_document_free(ui_document *);
