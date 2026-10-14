#include "runtime/json_query.h"

#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

void json_query_result_free(json_query_result *result)
{
    free(result->string);
    free(result->json);
    *result = (json_query_result){ 0 };
}
static int compare_keys(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}
static const char *validate(yyjson_val *value, int depth, int *remaining)
{
    if (depth > JSON_QUERY_MAX_DEPTH || --*remaining < 0) {
        return "JSON exceeds the nesting or value-count limit";
    }
    if (yyjson_is_str(value) && strlen(yyjson_get_str(value)) != yyjson_get_len(value)) {
        return "JSON strings cannot contain embedded NUL bytes";
    }
    if (yyjson_is_num(value) && !isfinite(yyjson_get_num(value))) {
        return "JSON numbers must be finite";
    }
    size_t index, count;
    yyjson_val *key, *child;
    if (yyjson_is_arr(value)) {
        yyjson_arr_foreach(value, index, count, child)
        {
            const char *error = validate(child, depth + 1, remaining);
            if (error) {
                return error;
            }
        }
    }
    if (yyjson_is_obj(value)) {
        size_t members = yyjson_obj_size(value);
        if (members > (size_t)*remaining) {
            return "JSON exceeds the value-count limit";
        }
        const char **keys = xmalloc((members ? members : 1) * sizeof *keys);
        const char *error = NULL;
        yyjson_obj_foreach(value, index, count, key, child)
        {
            keys[index] = yyjson_get_str(key);
            error = validate(key, depth + 1, remaining);
            if (!error) {
                error = validate(child, depth + 1, remaining);
            }
            if (error) {
                break;
            }
        }
        if (!error) {
            qsort(keys, members, sizeof *keys, compare_keys);
            for (size_t i = 1; i < members; i++) {
                if (!strcmp(keys[i - 1], keys[i])) {
                    error = "JSON objects cannot contain duplicate keys";
                    break;
                }
            }
        }
        free(keys);
        return error;
    }
    return NULL;
}
static bool valid_pointer(const char *pointer)
{
    size_t size = strnlen(pointer, JSON_QUERY_MAX_POINTER + 1);
    if (size > JSON_QUERY_MAX_POINTER || (size && pointer[0] != '/')) {
        return false;
    }
    for (size_t i = 0; i < size; i++) {
        if (pointer[i] == '~' &&
            (++i == size || (pointer[i] != '0' && pointer[i] != '1'))) {
            return false;
        }
    }
    return true;
}
/* Bound parser allocation before it builds its value table. The full parser
 * still owns JSON syntax and UTF-8 validation; separators only provide an
 * upper bound on containers and scalar counts here. */
static const char *preflight(const char *text)
{
    bool string = false, escaped = false;
    int depth = 0, separators = 0;
    for (const char *p = text; *p; p++) {
        if (string) {
            if (escaped) {
                escaped = false;
            } else if (*p == '\\') {
                escaped = true;
            } else if (*p == '"') {
                string = false;
            }
            continue;
        }
        if (*p == '"') {
            string = true;
        } else if (*p == '[' || *p == '{') {
            if (++depth > JSON_QUERY_MAX_DEPTH + 1) {
                return "JSON exceeds the nesting limit";
            }
        } else if (*p == ']' || *p == '}') {
            depth--;
        } else if (*p == ',' || *p == ':') {
            if (++separators > JSON_QUERY_MAX_VALUES * 2) {
                return "JSON exceeds the value-count limit";
            }
        }
    }
    return NULL;
}
bool query_json(const char *text, const char *pointer, json_query_result *result, char *error, size_t error_size)
{
    json_query_result_free(result);
    if (error_size) {
        error[0] = 0;
    }
    const char *failure = NULL;
    yyjson_doc *doc = NULL;
    if (!text || strnlen(text, JSON_QUERY_MAX_BYTES + 1) > JSON_QUERY_MAX_BYTES) {
        failure = "JSON text exceeds the 32 MiB limit";
    } else if (!pointer || !valid_pointer(pointer)) {
        failure = "Expected a bounded RFC 6901 JSON pointer";
    } else {
        failure = preflight(text);
        if (!failure) {
            doc = yyjson_read(text, strlen(text), 0);
            if (!doc) {
                failure = "Invalid UTF-8 JSON";
            }
        }
    }
    if (doc) {
        int remaining = JSON_QUERY_MAX_VALUES;
        failure = validate(yyjson_doc_get_root(doc), 0, &remaining);
    }
    if (failure) {
        snprintf(error, error_size, "%s", failure);
        yyjson_doc_free(doc);
        return false;
    }
    yyjson_val *value = yyjson_doc_ptr_get(doc, pointer);
    result->found = value != NULL;
    result->kind = !value                ? "missing"
                 : yyjson_is_obj(value)  ? "object"
                 : yyjson_is_arr(value)  ? "array"
                 : yyjson_is_str(value)  ? "string"
                 : yyjson_is_bool(value) ? "boolean"
                 : yyjson_is_int(value)  ? "integer"
                 : yyjson_is_num(value)  ? "number"
                                         : "null";
    result->string = xstrdup(yyjson_is_str(value) ? yyjson_get_str(value) : "");
    result->json = value ? yyjson_val_write(value, 0, NULL) : xstrdup("");
    result->number = yyjson_is_num(value) ? yyjson_get_num(value) : 0;
    result->boolean = yyjson_get_bool(value);
    result->count = (int)(yyjson_is_obj(value)   ? yyjson_obj_size(value)
                          : yyjson_is_str(value) ? yyjson_get_len(value)
                                                 : yyjson_arr_size(value));
    yyjson_doc_free(doc);
    return true;
}
