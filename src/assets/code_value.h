#ifndef CHIBI_CODE_VALUE_H
#define CHIBI_CODE_VALUE_H

#include "support/asset_value.h"

/* Behavior literals use source-language strings, numbers, booleans, arrays and
 * struct initializers. This API does not parse an asset or executable program.
 * The returned document owns the value; written text is malloc-owned. */
av_doc *code_value_read(const char *text, char *error, size_t size);
char *code_value_write(const av_value *value);

#endif
