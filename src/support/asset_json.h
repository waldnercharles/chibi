#ifndef CHIBI_ASSET_JSON_H
#define CHIBI_ASSET_JSON_H

#include "support/asset_value.h"

#include <yyjson.h>

/* JSON exists at protocol boundaries. These copy values into the receiving
 * document; asset loaders and editing models use native values. */
av_value *av_from_json(av_doc *doc, const yyjson_val *value);
yyjson_mut_val *av_to_json(yyjson_mut_doc *doc, const av_value *value);
av_doc *av_json_read(const char *text);
char *av_json_write(const av_value *value);

#endif
