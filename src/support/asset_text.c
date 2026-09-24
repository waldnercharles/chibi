#include "support/asset_text.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <yyjson.h>

static void strip_automatic_positions(yyjson_mut_val *value)
{
    if (yyjson_mut_is_arr(value)) {
        size_t i, count;
        yyjson_mut_val *item;
        yyjson_mut_arr_foreach(value, i, count, item)
        {
            strip_automatic_positions(item);
        }
        return;
    }
    if (!yyjson_mut_is_obj(value)) {
        return;
    }
    yyjson_mut_val *placed = yyjson_mut_obj_get(value, "placed");
    if (yyjson_mut_is_false(placed)) {
        yyjson_mut_obj_remove_key(value, "x");
        yyjson_mut_obj_remove_key(value, "y");
        yyjson_mut_obj_remove_key(value, "placed");
    }
    size_t i, count;
    yyjson_mut_val *key, *item;
    yyjson_mut_obj_foreach(value, i, count, key, item)
    {
        strip_automatic_positions(item);
    }
}

char *asset_text_to_disk(const char *json, char *error, size_t error_size)
{
    yyjson_doc *parsed = yyjson_read(json, strlen(json), 0);
    if (!parsed) {
        snprintf(error, error_size, "asset document is not valid JSON");
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(parsed, NULL);
    yyjson_doc_free(parsed);
    strip_automatic_positions(yyjson_mut_doc_get_root(doc));
    char *text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_mut_doc_free(doc);
    if (!text) {
        snprintf(error, error_size, "asset document could not be written");
    }
    return text;
}
