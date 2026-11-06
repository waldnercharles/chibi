#ifndef CHIBI_ASSET_VALUE_H
#define CHIBI_ASSET_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct av_doc av_doc;
typedef struct av_value av_value;
typedef enum
{
    AV_NULL,
    AV_BOOL,
    AV_SINT,
    AV_UINT,
    AV_REAL,
    AV_STRING,
    AV_OBJECT,
    AV_ARRAY
} av_type;
typedef struct
{
    size_t start, end, name_start, name_end;
} av_source;

/* A document owns every value allocated in it, including detached/replaced values.
 * Values inserted into a container must belong to the same document. Clone to
 * cross document ownership. Source offsets are bytes; SIZE_MAX means unknown. */
av_doc *av_doc_new();
void av_doc_free(av_doc *doc);
av_value *av_doc_root(const av_doc *doc);
void av_doc_set_root(av_doc *doc, av_value *value);
av_doc *av_doc_clone_value(const av_value *value);
av_value *av_value_copy(const av_value *value);
void av_value_free(av_value *value);
av_value *av_clone(av_doc *doc, const av_value *value);
bool av_equals(const av_value *a, const av_value *b);
/* Includes object member order for documents with ordered keyed fields. */
bool av_equals_ordered(const av_value *a, const av_value *b);
av_type av_get_type(const av_value *value);
av_source av_get_source(const av_value *value);
void av_set_source(av_value *value, av_source source);

av_value *av_null(av_doc *doc);
av_value *av_bool(av_doc *doc, bool value);
av_value *av_sint(av_doc *doc, int64_t value);
av_value *av_uint(av_doc *doc, uint64_t value);
/* Numeric storage with hexadecimal presentation; colors use eight digits. */
av_value *av_hex(av_doc *doc, uint64_t value);
av_value *av_color(av_doc *doc, uint32_t value);
av_value *av_real(av_doc *doc, double value);
/* Canonical decimal promotion preserves the original float on conversion back,
 * so saving a float does not expose digits introduced by widening to double. */
av_value *av_float(av_doc *doc, float value);
av_value *av_str(av_doc *doc, const char *value);
av_value *av_strn(av_doc *doc, const char *value, size_t len);
av_value *av_obj(av_doc *doc);
av_value *av_arr(av_doc *doc);
void av_set_str(av_value *value, const char *text);

bool av_is_null(const av_value *value);
bool av_is_bool(const av_value *value);
bool av_is_sint(const av_value *value);
bool av_is_uint(const av_value *value);
bool av_is_int(const av_value *value);
bool av_is_num(const av_value *value);
bool av_is_hex(const av_value *value);
bool av_is_color(const av_value *value);
bool av_is_str(const av_value *value);
bool av_is_obj(const av_value *value);
bool av_is_arr(const av_value *value);
bool av_is_ctn(const av_value *value);
bool av_get_bool(const av_value *value);
int64_t av_get_sint(const av_value *value);
uint64_t av_get_uint(const av_value *value);
int av_get_int(const av_value *value);
double av_get_num(const av_value *value);
const char *av_get_str(const av_value *value);
size_t av_get_len(const av_value *value);

av_value *av_obj_get(const av_value *obj, const char *key);
size_t av_obj_size(const av_value *obj);
bool av_obj_add(av_value *obj, av_value *key, av_value *value);
bool av_obj_set(av_doc *doc, av_value *obj, const char *key, av_value *value);
bool av_obj_add_val(av_doc *doc, av_value *obj, const char *key, av_value *value);
bool av_obj_remove_key(av_value *obj, const char *key);
bool av_obj_remove_str(av_value *obj, const char *key);
bool av_obj_replace(av_value *obj, av_value *key, av_value *value);
av_value *av_obj_key_at(const av_value *obj, size_t index);
av_value *av_obj_value_at(const av_value *obj, size_t index);
av_value *av_obj_add_obj(av_doc *doc, av_value *obj, const char *key);
av_value *av_obj_add_arr(av_doc *doc, av_value *obj, const char *key);
bool av_obj_add_str(av_doc *doc, av_value *obj, const char *key, const char *value);
bool av_obj_add_strcpy(av_doc *doc, av_value *obj, const char *key, const char *value);
bool av_obj_add_bool(av_doc *doc, av_value *obj, const char *key, bool value);
bool av_obj_add_int(av_doc *doc, av_value *obj, const char *key, int64_t value);
bool av_obj_add_uint(av_doc *doc, av_value *obj, const char *key, uint64_t value);
bool av_obj_add_float(av_doc *doc, av_value *obj, const char *key, float value);
bool av_obj_add_null(av_doc *doc, av_value *obj, const char *key);

size_t av_arr_size(const av_value *arr);
av_value *av_arr_get(const av_value *arr, size_t index);
bool av_arr_append(av_value *arr, av_value *value);
bool av_arr_add_val(av_value *arr, av_value *value);
av_value *av_arr_replace(av_value *arr, size_t index, av_value *value);
/* Detach an item while preserving order; its document retains ownership. */
av_value *av_arr_remove(av_value *arr, size_t index);
av_value *av_arr_add_obj(av_doc *doc, av_value *arr);
bool av_arr_add_str(av_doc *doc, av_value *arr, const char *value);
bool av_arr_add_strcpy(av_doc *doc, av_value *arr, const char *value);
bool av_arr_add_uint(av_doc *doc, av_value *arr, uint64_t value);

typedef struct
{
    const av_value *container;
    size_t index;
} av_arr_iter;
typedef struct
{
    const av_value *container;
    size_t index;
} av_obj_iter;
av_arr_iter av_arr_iter_with(const av_value *arr);
bool av_arr_iter_init(const av_value *arr, av_arr_iter *iter);
av_value *av_arr_iter_next(av_arr_iter *iter);
av_obj_iter av_obj_iter_with(const av_value *obj);
av_value *av_obj_iter_next(av_obj_iter *iter);
av_value *av_obj_iter_get_val(const av_value *key);

#define av_arr_foreach(arr, idx, max, val)                                     \
    for ((idx) = 0, (max) = av_arr_size(arr);                                  \
         (idx) < (max) && ((val) = av_arr_get(arr, idx), true);                \
         (idx)++)
#define av_obj_foreach(obj, idx, max, key, val)                                      \
    for ((idx) = 0, (max) = av_obj_size(obj);                                        \
         (idx) < (max) &&                                                            \
         ((key) = av_obj_key_at(obj, idx), (val) = av_obj_value_at(obj, idx), true); \
         (idx)++)

#endif
