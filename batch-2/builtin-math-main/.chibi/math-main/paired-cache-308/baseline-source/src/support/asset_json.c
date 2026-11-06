#include "support/asset_json.h"

#include <string.h>

av_value *av_from_json(av_doc *doc, const yyjson_val *value)
{
    if (!value) {
        return NULL;
    }
    if (yyjson_is_null(value)) {
        return av_null(doc);
    }
    if (yyjson_is_bool(value)) {
        return av_bool(doc, yyjson_get_bool(value));
    }
    if (yyjson_is_uint(value)) {
        return av_uint(doc, yyjson_get_uint(value));
    }
    if (yyjson_is_sint(value)) {
        return av_sint(doc, yyjson_get_sint(value));
    }
    if (yyjson_is_real(value)) {
        return av_real(doc, yyjson_get_real(value));
    }
    if (yyjson_is_str(value)) {
        return av_strn(doc, yyjson_get_str(value), yyjson_get_len(value));
    }
    av_value *out = yyjson_is_obj(value) ? av_obj(doc) : av_arr(doc);
    size_t i, count;
    yyjson_val *key, *item;
    if (yyjson_is_obj(value)) {
        yyjson_obj_foreach(value, i, count, key, item)
        {
            av_obj_set(doc, out, yyjson_get_str(key), av_from_json(doc, item));
        }
        return out;
    }
    yyjson_arr_foreach(value, i, count, item)
    {
        av_arr_append(out, av_from_json(doc, item));
    }
    return out;
}

yyjson_mut_val *av_to_json(yyjson_mut_doc *doc, const av_value *value)
{
    if (!value) {
        return NULL;
    }
    switch (av_get_type(value)) {
        case AV_NULL: return yyjson_mut_null(doc);
        case AV_BOOL: return yyjson_mut_bool(doc, av_get_bool(value));
        case AV_SINT: return yyjson_mut_sint(doc, av_get_sint(value));
        case AV_UINT: return yyjson_mut_uint(doc, av_get_uint(value));
        case AV_REAL: return yyjson_mut_real(doc, av_get_num(value));
        case AV_STRING:
            return yyjson_mut_strncpy(doc, av_get_str(value), av_get_len(value));
        case AV_OBJECT: {
            yyjson_mut_val *out = yyjson_mut_obj(doc);
            for (size_t i = 0; i < av_obj_size(value); i++) {
                const av_value *key = av_obj_key_at(value, i);
                yyjson_mut_obj_add(
                    out,
                    yyjson_mut_strncpy(doc, av_get_str(key), av_get_len(key)),
                    av_to_json(doc, av_obj_value_at(value, i))
                );
            }
            return out;
        }
        case AV_ARRAY: {
            yyjson_mut_val *out = yyjson_mut_arr(doc);
            for (size_t i = 0; i < av_arr_size(value); i++) {
                yyjson_mut_arr_append(out, av_to_json(doc, av_arr_get(value, i)));
            }
            return out;
        }
    }
    return NULL;
}

av_doc *av_json_read(const char *text)
{
    yyjson_doc *input = text ? yyjson_read(text, strlen(text), 0) : NULL;
    if (!input) {
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_doc_set_root(doc, av_from_json(doc, yyjson_doc_get_root(input)));
    yyjson_doc_free(input);
    return doc;
}

char *av_json_write(const av_value *value)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(doc, av_to_json(doc, value));
    char *text = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return text;
}
