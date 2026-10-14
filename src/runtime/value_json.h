#ifndef CHIBI_VALUE_JSON_H
#define CHIBI_VALUE_JSON_H
#include "types/value.h"

#define VALUE_JSON_MAX_BYTES (1024 * 1024)
#define VALUE_JSON_MAX_DEPTH 16
#define VALUE_JSON_MAX_VALUES 65536
/* Owned JSON using the same scalar/record/array representation as asset
 * documents. Sets encode as arrays, enums use their reflected labels, tagged
 * unions use {tag,values}; flow/absent records encode null. Tasks and maps are
 * refused. Validates finite numbers and bounded traversal before allocating a
 * document. Refusal leaves *text NULL and writes a human-readable error. */
bool value_to_json(const pin_value *, char **text, char *error, size_t error_size);
#endif
