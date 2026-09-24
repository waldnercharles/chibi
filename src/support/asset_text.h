#ifndef CHIBI_ASSET_TEXT_H
#define CHIBI_ASSET_TEXT_H

#include <stddef.h>
#include <yyjson.h>

/* Asset files are YAML; in-memory documents are JSON text. */
#define ASSET_EXTENSION ".yaml"

/* Parses asset file YAML or in-memory JSON. Returns NULL with a line and
 * column in `error` when the text is invalid. */
yyjson_doc *asset_text_read(const char *text, char *error, size_t error_size);
/* Owned asset file YAML for an in-memory JSON document or asset text. Files
 * omit the positions of nodes marked "placed": false; loading leaves those
 * nodes automatic for the editor to arrange. */
char *asset_text_to_disk(const char *text, char *error, size_t error_size);

#endif
