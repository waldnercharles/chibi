#ifndef CHIBI_AUTHORING_SOURCE_PRESENTATION_ANCHORS_H
#define CHIBI_AUTHORING_SOURCE_PRESENTATION_ANCHORS_H
#include "authoring/source_presentation_internal.h"

#include <yyjson.h>

void source_presentation_anchors_write(yyjson_mut_doc *, yyjson_mut_val *, const authoring_source_presentation *);
/* Returns owned JSON with origin keys rebound only after complete table
 * pairing. The caller must check exact source bytes before calling and validate
 * all layout and topology before applying the returned snapshot. */
char *source_presentation_anchors_resolve(const char *, const authoring_source_presentation *);
#endif
