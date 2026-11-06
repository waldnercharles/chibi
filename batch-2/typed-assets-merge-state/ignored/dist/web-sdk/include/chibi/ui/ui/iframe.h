#ifndef CHIBI_UI_IFRAME_H
#define CHIBI_UI_IFRAME_H

/* Documents shown in an element of another document, as an <iframe> shows
 * one in its box. RmlUi lays out a whole document whenever anything in it
 * changes, so a large UI splits into documents that lay out apart: a change
 * inside one lays out only that one. They share their context, so focus,
 * hover, keys, data models and textures cross between them.
 *
 * After each update the document takes its element's border box. With
 * `fit` it takes only the width and the element takes the document's
 * height instead, as a box sized to its content does. */

#include "ui/rml.h"

#include <stdbool.h>

void iframe_attach(rml_element *document, rml_element *element, bool fit);
void iframe_detach(rml_element *document);
/* Lays out the documents holding `context`'s elements, then gives each
 * shown document its element's box, and each fitting element its
 * document's height, laying out again what moved until nothing does. The
 * backend calls it before each update, so a window resize or a moved
 * splitter lays each document out once, and after, for what the update
 * changed. */
void iframes_settle(rml_context *context);

#endif
