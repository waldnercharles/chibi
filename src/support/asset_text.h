#ifndef CHIBI_ASSET_TEXT_H
#define CHIBI_ASSET_TEXT_H

#include "support/asset_format.h"
#include "support/asset_value.h"

/* KDL 2 source decodes directly into native asset values. The root is an
 * implicit object whose keys are the document's top-level node names. */
av_doc *av_doc_read(const char *text, char *error, size_t error_size);
char *av_doc_write(const av_doc *doc);
/* A scalar literal, or the contents of an implicit object/array root. */
char *av_value_write(const av_value *value);
av_doc *av_value_read(const char *text, char *error, size_t error_size);
av_doc *asset_text_read(const char *text, char *error, size_t error_size);
char *asset_text_write(const av_value *root);
char *asset_text_to_disk(const char *text, char *error, size_t error_size);

#endif
