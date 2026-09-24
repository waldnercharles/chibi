#ifndef CHIBI_ASSET_TEXT_H
#define CHIBI_ASSET_TEXT_H

#include <stddef.h>

/* Owned asset file text for an in-memory JSON document. Files omit the
 * positions of nodes marked "placed": false; loading leaves those nodes
 * automatic for the editor to arrange. */
char *asset_text_to_disk(const char *json, char *error, size_t error_size);

#endif
