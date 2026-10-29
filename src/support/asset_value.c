#include "support/asset_value.h"

#include "support/array.h"
#include "support/float_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    av_value *key, *value;
} av_field;
struct av_value
{
    av_doc *owner;
    av_type type;
    bool hex, color;
    av_source source;
    av_value *field_value;
    union
    {
        bool boolean;
        int64_t sint;
        uint64_t uint;
        double real;
        struct
        {
            char *text;
            size_t len;
        } string;
        av_field *fields;
        av_value **items;
    } data;
};
struct av_doc
{
    av_value *root;
    av_value **values;
};

av_doc *av_doc_new()
{
    return xcalloc(1, sizeof(av_doc));
}
void av_doc_free(av_doc *doc)
{
    if (!doc) {
        return;
    }
    for (int i = 0; i < acount(doc->values); i++) {
        av_value *v = doc->values[i];
        if (v->type == AV_STRING) {
            free(v->data.string.text);
        }
        if (v->type == AV_OBJECT) {
            afree(v->data.fields);
        }
        if (v->type == AV_ARRAY) {
            afree(v->data.items);
        }
        free(v);
    }
    afree(doc->values);
    free(doc);
}
av_value *av_doc_root(const av_doc *doc)
{
    return doc ? doc->root : NULL;
}
void av_doc_set_root(av_doc *doc, av_value *value)
{
    assert(doc && (!value || value->owner == doc));
    doc->root = value;
}
static av_value *new_value(av_doc *doc, av_type type)
{
    assert(doc);
    av_value *v = xcalloc(1, sizeof(*v));
    v->owner = doc;
    v->type = type;
    v->source = (av_source){ SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX };
    apush(doc->values, v);
    return v;
}
av_value *av_null(av_doc *doc)
{
    return new_value(doc, AV_NULL);
}
av_value *av_bool(av_doc *doc, bool value)
{
    av_value *v = new_value(doc, AV_BOOL);
    v->data.boolean = value;
    return v;
}
av_value *av_sint(av_doc *doc, int64_t value)
{
    av_value *v = new_value(doc, AV_SINT);
    v->data.sint = value;
    return v;
}
av_value *av_uint(av_doc *doc, uint64_t value)
{
    av_value *v = new_value(doc, AV_UINT);
    v->data.uint = value;
    return v;
}
av_value *av_hex(av_doc *doc, uint64_t value)
{
    av_value *v = av_uint(doc, value);
    v->hex = true;
    return v;
}
av_value *av_color(av_doc *doc, uint32_t value)
{
    av_value *v = av_hex(doc, value);
    v->color = true;
    return v;
}
av_value *av_real(av_doc *doc, double value)
{
    av_value *v = new_value(doc, AV_REAL);
    v->data.real = value;
    return v;
}
av_value *av_float(av_doc *doc, float value)
{
    char text[64];
    float_text(text, sizeof text, value, true);
    return av_real(doc, strtod(text, NULL));
}
av_value *av_strn(av_doc *doc, const char *value, size_t len)
{
    assert(value || !len);
    av_value *v = new_value(doc, AV_STRING);
    v->data.string.text = xmalloc(len + 1);
    if (len) {
        memcpy(v->data.string.text, value, len);
    }
    v->data.string.text[len] = 0;
    v->data.string.len = len;
    return v;
}
av_value *av_str(av_doc *doc, const char *value)
{
    assert(value);
    return av_strn(doc, value, strlen(value));
}
av_value *av_obj(av_doc *doc)
{
    return new_value(doc, AV_OBJECT);
}
av_value *av_arr(av_doc *doc)
{
    return new_value(doc, AV_ARRAY);
}
void av_set_str(av_value *value, const char *text)
{
    assert(av_is_str(value) && text);
    char *copy = xstrdup(text);
    free(value->data.string.text);
    value->data.string.text = copy;
    value->data.string.len = strlen(copy);
}
av_type av_get_type(const av_value *v)
{
    assert(v);
    return v->type;
}
av_source av_get_source(const av_value *v)
{
    assert(v);
    return v->source;
}
void av_set_source(av_value *v, av_source source)
{
    assert(v);
    v->source = source;
}
bool av_is_hex(const av_value *v)
{
    return v && v->type == AV_UINT && v->hex;
}
bool av_is_color(const av_value *v)
{
    return v && v->type == AV_UINT && v->color;
}
bool av_is_null(const av_value *v)
{
    return v && v->type == AV_NULL;
}
bool av_is_bool(const av_value *v)
{
    return v && v->type == AV_BOOL;
}
bool av_is_sint(const av_value *v)
{
    return v && v->type == AV_SINT;
}
bool av_is_uint(const av_value *v)
{
    return v && v->type == AV_UINT;
}
bool av_is_int(const av_value *v)
{
    return av_is_sint(v) || av_is_uint(v);
}
bool av_is_num(const av_value *v)
{
    return av_is_int(v) || (v && v->type == AV_REAL);
}
bool av_is_str(const av_value *v)
{
    return v && v->type == AV_STRING;
}
bool av_is_obj(const av_value *v)
{
    return v && v->type == AV_OBJECT;
}
bool av_is_arr(const av_value *v)
{
    return v && v->type == AV_ARRAY;
}
bool av_is_ctn(const av_value *v)
{
    return av_is_obj(v) || av_is_arr(v);
}
bool av_get_bool(const av_value *v)
{
    return av_is_bool(v) && v->data.boolean;
}
int64_t av_get_sint(const av_value *v)
{
    if (av_is_sint(v)) {
        return v->data.sint;
    }
    if (av_is_uint(v)) {
        return (int64_t)v->data.uint;
    }
    return 0;
}
uint64_t av_get_uint(const av_value *v)
{
    if (av_is_uint(v)) {
        return v->data.uint;
    }
    if (av_is_sint(v)) {
        return (uint64_t)v->data.sint;
    }
    return 0;
}
int av_get_int(const av_value *v)
{
    return (int)av_get_sint(v);
}
double av_get_num(const av_value *v)
{
    if (av_is_sint(v)) {
        return (double)v->data.sint;
    }
    if (av_is_uint(v)) {
        return (double)v->data.uint;
    }
    if (v && v->type == AV_REAL) {
        return v->data.real;
    }
    return 0;
}
const char *av_get_str(const av_value *v)
{
    return av_is_str(v) ? v->data.string.text : NULL;
}
size_t av_get_len(const av_value *v)
{
    return av_is_str(v) ? v->data.string.len : 0;
}
static int field_index(const av_value *obj, const char *key)
{
    if (!av_is_obj(obj) || !key) {
        return -1;
    }
    for (int i = 0; i < acount(obj->data.fields); i++) {
        if (!strcmp(av_get_str(obj->data.fields[i].key), key)) {
            return i;
        }
    }
    return -1;
}
av_value *av_obj_get(const av_value *obj, const char *key)
{
    int i = field_index(obj, key);
    return i < 0 ? NULL : obj->data.fields[i].value;
}
size_t av_obj_size(const av_value *obj)
{
    return av_is_obj(obj) ? (size_t)acount(obj->data.fields) : 0;
}
bool av_obj_add(av_value *obj, av_value *key, av_value *value)
{
    assert(av_is_obj(obj) && av_is_str(key) && value);
    assert(obj->owner == key->owner && obj->owner == value->owner && obj != value);
    int i = field_index(obj, av_get_str(key));
    key->field_value = value;
    if (i >= 0) {
        obj->data.fields[i].key = key;
        obj->data.fields[i].value = value;
        return true;
    }
    apush(obj->data.fields, ((av_field){ key, value }));
    return true;
}
bool av_obj_set(av_doc *doc, av_value *obj, const char *key, av_value *value)
{
    assert(doc && obj && obj->owner == doc && value && value->owner == doc);
    int i = field_index(obj, key);
    if (i >= 0) {
        obj->data.fields[i].value = value;
        obj->data.fields[i].key->field_value = value;
        return true;
    }
    return av_obj_add(obj, av_str(doc, key), value);
}
bool av_obj_add_val(av_doc *doc, av_value *obj, const char *key, av_value *value)
{
    return av_obj_set(doc, obj, key, value);
}
bool av_obj_remove_key(av_value *obj, const char *key)
{
    assert(av_is_obj(obj));
    int i = field_index(obj, key);
    if (i < 0) {
        return false;
    }
    obj->data.fields[i].key->field_value = NULL;
    int count = acount(obj->data.fields);
    memmove(obj->data.fields + i, obj->data.fields + i + 1, (size_t)(count - i - 1) * sizeof(av_field));
    asetlen(obj->data.fields, count - 1);
    return true;
}
bool av_obj_remove_str(av_value *obj, const char *key)
{
    return av_obj_remove_key(obj, key);
}
av_value *av_obj_key_at(const av_value *obj, size_t index)
{
    return index < av_obj_size(obj) ? obj->data.fields[index].key : NULL;
}
av_value *av_obj_value_at(const av_value *obj, size_t index)
{
    return index < av_obj_size(obj) ? obj->data.fields[index].value : NULL;
}
av_value *av_obj_add_obj(av_doc *doc, av_value *obj, const char *key)
{
    av_value *v = av_obj(doc);
    av_obj_set(doc, obj, key, v);
    return v;
}
av_value *av_obj_add_arr(av_doc *doc, av_value *obj, const char *key)
{
    av_value *v = av_arr(doc);
    av_obj_set(doc, obj, key, v);
    return v;
}
bool av_obj_add_str(av_doc *doc, av_value *obj, const char *key, const char *value)
{
    return av_obj_set(doc, obj, key, av_str(doc, value));
}
bool av_obj_add_strcpy(av_doc *doc, av_value *obj, const char *key, const char *value)
{
    return av_obj_add_str(doc, obj, key, value);
}
bool av_obj_add_bool(av_doc *doc, av_value *obj, const char *key, bool value)
{
    return av_obj_set(doc, obj, key, av_bool(doc, value));
}
bool av_obj_add_int(av_doc *doc, av_value *obj, const char *key, int64_t value)
{
    return av_obj_set(doc, obj, key, av_sint(doc, value));
}
bool av_obj_add_uint(av_doc *doc, av_value *obj, const char *key, uint64_t value)
{
    return av_obj_set(doc, obj, key, av_uint(doc, value));
}
bool av_obj_add_float(av_doc *doc, av_value *obj, const char *key, float value)
{
    return av_obj_set(doc, obj, key, av_float(doc, value));
}
bool av_obj_add_null(av_doc *doc, av_value *obj, const char *key)
{
    return av_obj_set(doc, obj, key, av_null(doc));
}
size_t av_arr_size(const av_value *arr)
{
    return av_is_arr(arr) ? (size_t)acount(arr->data.items) : 0;
}
av_value *av_arr_get(const av_value *arr, size_t index)
{
    return index < av_arr_size(arr) ? arr->data.items[index] : NULL;
}
bool av_arr_append(av_value *arr, av_value *value)
{
    assert(av_is_arr(arr) && value && arr->owner == value->owner && arr != value);
    apush(arr->data.items, value);
    return true;
}
bool av_arr_add_val(av_value *arr, av_value *value)
{
    return av_arr_append(arr, value);
}
av_value *av_arr_replace(av_value *arr, size_t index, av_value *value)
{
    assert(av_is_arr(arr) && value && arr->owner == value->owner && arr != value);
    if (index >= av_arr_size(arr)) {
        return NULL;
    }
    av_value *old = arr->data.items[index];
    arr->data.items[index] = value;
    return old;
}
av_value *av_arr_remove(av_value *arr, size_t index)
{
    assert(av_is_arr(arr));
    size_t count = av_arr_size(arr);
    if (index >= count) {
        return NULL;
    }
    av_value *value = arr->data.items[index];
    memmove(
        arr->data.items + index,
        arr->data.items + index + 1,
        (count - index - 1) * sizeof *arr->data.items
    );
    asetlen(arr->data.items, (int)count - 1);
    return value;
}
av_value *av_arr_add_obj(av_doc *doc, av_value *arr)
{
    av_value *v = av_obj(doc);
    av_arr_append(arr, v);
    return v;
}
bool av_arr_add_str(av_doc *doc, av_value *arr, const char *value)
{
    return av_arr_append(arr, av_str(doc, value));
}
bool av_arr_add_strcpy(av_doc *doc, av_value *arr, const char *value)
{
    return av_arr_add_str(doc, arr, value);
}
bool av_arr_add_uint(av_doc *doc, av_value *arr, uint64_t value)
{
    return av_arr_append(arr, av_uint(doc, value));
}
av_arr_iter av_arr_iter_with(const av_value *arr)
{
    return (av_arr_iter){ arr, 0 };
}
bool av_arr_iter_init(const av_value *arr, av_arr_iter *iter)
{
    assert(iter);
    *iter = av_arr_iter_with(arr);
    return av_is_arr(arr);
}
av_value *av_arr_iter_next(av_arr_iter *iter)
{
    assert(iter);
    return av_arr_get(iter->container, iter->index++);
}
av_obj_iter av_obj_iter_with(const av_value *obj)
{
    return (av_obj_iter){ obj, 0 };
}
av_value *av_obj_iter_next(av_obj_iter *iter)
{
    assert(iter);
    return av_obj_key_at(iter->container, iter->index++);
}
av_value *av_obj_iter_get_val(const av_value *key)
{
    return av_is_str(key) ? key->field_value : NULL;
}

av_value *av_clone(av_doc *doc, const av_value *value)
{
    assert(doc && value);
    av_value *copy = NULL;
    switch (value->type) {
        case AV_NULL: copy = av_null(doc); break;
        case AV_BOOL: copy = av_bool(doc, value->data.boolean); break;
        case AV_SINT: copy = av_sint(doc, value->data.sint); break;
        case AV_UINT: copy = av_uint(doc, value->data.uint); break;
        case AV_REAL: copy = av_real(doc, value->data.real); break;
        case AV_STRING:
            copy = av_strn(doc, value->data.string.text, value->data.string.len);
            break;
        case AV_OBJECT:
            copy = av_obj(doc);
            for (int i = 0; i < acount(value->data.fields); i++) {
                av_obj_add(
                    copy,
                    av_clone(doc, value->data.fields[i].key),
                    av_clone(doc, value->data.fields[i].value)
                );
            }
            break;
        case AV_ARRAY:
            copy = av_arr(doc);
            for (int i = 0; i < acount(value->data.items); i++) {
                av_arr_append(copy, av_clone(doc, value->data.items[i]));
            }
            break;
    }
    copy->hex = value->hex;
    copy->color = value->color;
    copy->source = value->source;
    return copy;
}
av_doc *av_doc_clone_value(const av_value *value)
{
    assert(value);
    av_doc *doc = av_doc_new();
    av_doc_set_root(doc, av_clone(doc, value));
    return doc;
}
av_value *av_value_copy(const av_value *value)
{
    return av_doc_root(av_doc_clone_value(value));
}
void av_value_free(av_value *value)
{
    if (!value) {
        return;
    }
    assert(value->owner->root == value);
    av_doc_free(value->owner);
}
static bool number_equals(const av_value *a, const av_value *b)
{
    if (a->type == AV_REAL && b->type == AV_REAL) {
        return a->data.real == b->data.real;
    }
    if (a->type == AV_REAL || b->type == AV_REAL) {
        const av_value *real = a->type == AV_REAL ? a : b;
        const av_value *integer = a->type == AV_REAL ? b : a;
        double number = real->data.real;
        if (integer->type == AV_UINT) {
            if (!(number >= 0 && number < 0x1p64)) {
                return false;
            }
            uint64_t converted = (uint64_t)number;
            return converted == integer->data.uint && (double)converted == number;
        }
        if (!(number >= -0x1p63 && number < 0x1p63)) {
            return false;
        }
        int64_t converted = (int64_t)number;
        return converted == integer->data.sint && (double)converted == number;
    }
    if (a->type == AV_SINT && b->type == AV_UINT) {
        return a->data.sint >= 0 && (uint64_t)a->data.sint == b->data.uint;
    }
    if (a->type == AV_UINT && b->type == AV_SINT) {
        return b->data.sint >= 0 && a->data.uint == (uint64_t)b->data.sint;
    }
    if (a->type == AV_SINT) {
        return a->data.sint == b->data.sint;
    }
    return a->data.uint == b->data.uint;
}
static bool value_equals(const av_value *a, const av_value *b, bool ordered)
{
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    if (av_is_num(a) && av_is_num(b)) {
        return number_equals(a, b);
    }
    if (a->type != b->type) {
        return false;
    }
    switch (a->type) {
        case AV_NULL: return true;
        case AV_BOOL: return a->data.boolean == b->data.boolean;
        case AV_STRING:
            return a->data.string.len == b->data.string.len &&
                   !memcmp(
                       a->data.string.text,
                       b->data.string.text,
                       a->data.string.len
                   );
        case AV_OBJECT:
            if (av_obj_size(a) != av_obj_size(b)) {
                return false;
            }
            for (int i = 0; i < acount(a->data.fields); i++) {
                if ((ordered &&
                     !value_equals(a->data.fields[i].key, b->data.fields[i].key, true)) ||
                    !value_equals(
                        a->data.fields[i].value,
                        ordered ? b->data.fields[i].value
                                : av_obj_get(b, av_get_str(a->data.fields[i].key)),
                        ordered
                    )) {
                    return false;
                }
            }
            return true;
        case AV_ARRAY:
            if (av_arr_size(a) != av_arr_size(b)) {
                return false;
            }
            for (int i = 0; i < acount(a->data.items); i++) {
                if (!value_equals(a->data.items[i], b->data.items[i], ordered)) {
                    return false;
                }
            }
            return true;
        case AV_SINT:
        case AV_UINT:
        case AV_REAL: return number_equals(a, b);
    }
    return false;
}

bool av_equals(const av_value *a, const av_value *b)
{
    return value_equals(a, b, false);
}

bool av_equals_ordered(const av_value *a, const av_value *b)
{
    return value_equals(a, b, true);
}
