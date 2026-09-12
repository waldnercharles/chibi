#include "graph/runtime/value_array.h"

#include "graph/model/graph.h"
#include "graph/model/registry.h"
#include "support/arena.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

static int value_string(pin_type type)
{
    return type == PIN_STR || type == PIN_NAME;
}

static char *arena_string(arena *a, const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t size = strlen(s) + 1;
    char *copy = (char *)arena_alloc(a, size);
    if (copy) {
        memcpy(copy, s, size);
    }
    return copy;
}

static char *graph_string(graph *g, const char *s)
{
    return s ? graph_own_str(g, s, strlen(s)) : NULL;
}

pin_array *pin_array_new(pin_type element_type, const char *type_name, int capacity)
{
    if (capacity < 0) {
        return NULL;
    }
    pin_array *array = (pin_array *)xcalloc(1, sizeof *array);
    array->element_type = element_type;
    array->type_name = type_name;
    if (capacity) {
        array->items = (pin_value *)xcalloc((size_t)capacity, sizeof *array->items);
        array->capacity = capacity;
    }
    return array;
}

int pin_array_reserve(pin_array *array, int capacity)
{
    if (!array || capacity < 0) {
        return -1;
    }
    if (capacity <= array->capacity) {
        return 0;
    }
    array->items = (pin_value *)
        xrealloc(array->items, (size_t)capacity * sizeof *array->items);
    memset(
        array->items + array->capacity,
        0,
        (size_t)(capacity - array->capacity) * sizeof *array->items
    );
    array->capacity = capacity;
    return 0;
}

int pin_array_push(pin_array *array, const pin_value *value)
{
    return pin_array_insert(array, array ? array->count : -1, value);
}

int pin_array_insert(pin_array *array, int index, const pin_value *value)
{
    if (!array || !value || index < 0 || index > array->count ||
        value->type != array->element_type) {
        return -1;
    }
    if (array->count == array->capacity) {
        int capacity = array->capacity ? array->capacity * 2 : 4;
        if (pin_array_reserve(array, capacity)) {
            return -1;
        }
    }
    pin_value copy = { 0 };
    if (pin_value_copy(&copy, value)) {
        return -1;
    }
    memmove(
        array->items + index + 1,
        array->items + index,
        (size_t)(array->count - index) * sizeof *array->items
    );
    array->items[index] = copy;
    array->count++;
    return index;
}

int pin_array_remove(pin_array *array, int index)
{
    if (!array || index < 0 || index >= array->count) {
        return -1;
    }
    pin_value_destroy(&array->items[index]);
    memmove(
        array->items + index,
        array->items + index + 1,
        (size_t)(array->count - index - 1) * sizeof *array->items
    );
    array->count--;
    return 0;
}

static pin_array *array_clone_heap(const pin_array *src)
{
    if (!src) {
        return NULL;
    }
    pin_array *dst = pin_array_new(src->element_type, src->type_name, src->count);
    dst->count = src->count;
    for (int i = 0; i < src->count; i++) {
        if (pin_value_copy(&dst->items[i], &src->items[i])) {
            pin_array_free(dst);
            return NULL;
        }
    }
    return dst;
}

pin_array *pin_array_clone(const pin_array *src)
{
    return array_clone_heap(src);
}

static pin_array *array_clone_graph(graph *g, const pin_array *src)
{
    if (!g || !src) {
        return NULL;
    }
    size_t size = sizeof(pin_array) + (size_t)src->count * sizeof(pin_value);
    pin_array *dst = (pin_array *)graph_own_block(g, size);
    dst->element_type = src->element_type;
    dst->type_name = graph_string(g, src->type_name);
    dst->count = src->count;
    dst->capacity = src->count;
    dst->items = (pin_value *)(dst + 1);
    memset(dst->items, 0, (size_t)src->count * sizeof *dst->items);
    for (int i = 0; i < src->count; i++) {
        if (pin_value_copy_to_graph(g, &dst->items[i], &src->items[i])) {
            return NULL;
        }
    }
    return dst;
}

pin_array *pin_array_clone_to_graph(graph *g, const pin_array *src)
{
    return array_clone_graph(g, src);
}

static pin_array *array_clone_arena(arena *a, const pin_array *src)
{
    if (!a || !src) {
        return NULL;
    }
    size_t size = sizeof(pin_array) + (size_t)src->count * sizeof(pin_value);
    pin_array *dst = (pin_array *)arena_alloc(a, size);
    if (!dst) {
        return NULL;
    }
    dst->element_type = src->element_type;
    dst->type_name = arena_string(a, src->type_name);
    dst->count = src->count;
    dst->capacity = src->count;
    dst->items = (pin_value *)(dst + 1);
    memset(dst->items, 0, (size_t)src->count * sizeof *dst->items);
    for (int i = 0; i < src->count; i++) {
        if (pin_value_copy_to_arena(a, &dst->items[i], &src->items[i])) {
            return NULL;
        }
    }
    return dst;
}

pin_array *pin_array_clone_to_arena(arena *a, const pin_array *src)
{
    return array_clone_arena(a, src);
}

void pin_array_free(pin_array *array)
{
    if (!array) {
        return;
    }
    for (int i = 0; i < array->count; i++) {
        pin_value_destroy(&array->items[i]);
    }
    free(array->items);
    free(array);
}

int pin_value_copy(pin_value *dst, const pin_value *src)
{
    if (!dst || !src) {
        return -1;
    }
    *dst = *src;
    if (src->container >= PIN_CONTAINER_ARRAY) {
        dst->ptr = pin_array_clone((const pin_array *)src->ptr);
        return !src->ptr || dst->ptr ? 0 : -1;
    }
    if (value_string(src->type) && src->str) {
        dst->str = xstrdup(src->str);
        return 0;
    }
    if (src->type == PIN_STRUCT && src->ptr) {
        return 0;
    }
    return 0;
}

int pin_value_copy_to_graph(graph *g, pin_value *dst, const pin_value *src)
{
    if (!g || !dst || !src) {
        return -1;
    }
    *dst = *src;
    if (src->container >= PIN_CONTAINER_ARRAY) {
        dst->ptr = pin_array_clone_to_graph(g, (const pin_array *)src->ptr);
        return !src->ptr || dst->ptr ? 0 : -1;
    }
    if (value_string(src->type) && src->str) {
        dst->str = graph_string(g, src->str);
    }
    if (src->type == PIN_STRUCT && src->ptr) {
        const type_desc *type = registry_type_by_name(src->type_name);
        if (!type || !type->size) {
            return -1;
        }
        void *copy = graph_own_block(g, type->size);
        memcpy(copy, src->ptr, type->size);
        for (int i = 0; i < type->field_count; i++) {
            const type_field *field = &type->fields[i];
            if (field->type != PIN_STRUCT && !value_string(field->type)) {
                continue;
            }
            pin_value member, owned;
            pin_read_field(src->ptr, field, &member);
            if (pin_value_copy_to_graph(g, &owned, &member)) {
                return -1;
            }
            pin_write_field(copy, field, owned);
        }
        dst->ptr = copy;
    }
    return 0;
}

int pin_value_copy_to_arena(arena *a, pin_value *dst, const pin_value *src)
{
    if (!a || !dst || !src) {
        return -1;
    }
    *dst = *src;
    if (src->container >= PIN_CONTAINER_ARRAY) {
        dst->ptr = pin_array_clone_to_arena(a, (const pin_array *)src->ptr);
        return !src->ptr || dst->ptr ? 0 : -1;
    }
    if (value_string(src->type) && src->str) {
        dst->str = arena_string(a, src->str);
    }
    return 0;
}

void pin_value_destroy(pin_value *value)
{
    if (!value) {
        return;
    }
    if (value->container >= PIN_CONTAINER_ARRAY) {
        pin_array_free((pin_array *)value->ptr);
    } else if (value_string(value->type)) {
        free((void *)value->str);
    }
    /* Struct values are borrowed until a type-specific copy callback exists.
     * Their storage may belong to an arena, graph, or host object. */
    memset(value, 0, sizeof *value);
}

int pin_value_equal(const pin_value *a, const pin_value *b)
{
    if (!a || !b || a->type != b->type || a->container != b->container) {
        return 0;
    }
    if (a->container >= PIN_CONTAINER_ARRAY) {
        return pin_array_equal((const pin_array *)a->ptr, (const pin_array *)b->ptr);
    }
    if (value_string(a->type)) {
        return strcmp(a->str ? a->str : "", b->str ? b->str : "") == 0;
    }
    if (a->type == PIN_STRUCT) {
        return a->ptr == b->ptr;
    }
    if (a->type == PIN_HANDLE) {
        return a->u64 == b->u64 &&
               strcmp(a->type_name ? a->type_name : "", b->type_name ? b->type_name : "") ==
                   0;
    }
    return memcmp(a, b, sizeof *a) == 0;
}

int pin_array_equal(const pin_array *a, const pin_array *b)
{
    if (!a || !b || a->element_type != b->element_type || a->count != b->count) {
        return 0;
    }
    for (int i = 0; i < a->count; i++) {
        if (!pin_value_equal(&a->items[i], &b->items[i])) {
            return 0;
        }
    }
    return 1;
}
