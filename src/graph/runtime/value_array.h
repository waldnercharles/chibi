#ifndef CHIBI_VALUE_ARRAY_H
#define CHIBI_VALUE_ARRAY_H

#include "graph/model/pin.h"

#include <stddef.h>

typedef struct graph graph;
typedef struct arena arena;

typedef struct pin_array
{
    pin_type element_type;
    const char *type_name;
    int count;
    int capacity;
    pin_value *items;
} pin_array;

/* TYPE-8/TYPE-9: arrays own their item storage. Struct elements remain
 * borrowed until a type-specific copy callback exists. */
pin_array *pin_array_new(pin_type element_type, const char *type_name, int capacity);
pin_array *pin_array_clone(const pin_array *src);
pin_array *pin_array_clone_to_graph(graph *g, const pin_array *src);
pin_array *pin_array_clone_to_arena(arena *a, const pin_array *src);
/* Fixed-size typed zero values, owned by the arena. */
pin_array *pin_array_allocate(arena *a, pin_type type, const char *name, int count);
/* Deep snapshot, including registered struct fields and all nominal names. */
pin_array *pin_array_snapshot_to_arena(arena *a, const pin_array *src);
int pin_value_snapshot_to_arena(arena *a, pin_value *dst, const pin_value *src);
void pin_array_free(pin_array *array);
int pin_array_reserve(pin_array *array, int capacity);
int pin_array_push(pin_array *array, const pin_value *value);
int pin_array_insert(pin_array *array, int index, const pin_value *value);
int pin_array_remove(pin_array *array, int index);
int pin_array_equal(const pin_array *a, const pin_array *b);

/* Copy and destruction dispatch. `dst` owns copied array and string storage;
 * struct and handle payloads remain borrowed. */
int pin_value_copy(pin_value *dst, const pin_value *src);
/* Graph copies also own registered structs, including nested string fields. */
int pin_value_copy_to_graph(graph *g, pin_value *dst, const pin_value *src);
int pin_value_copy_to_arena(arena *a, pin_value *dst, const pin_value *src);
void pin_value_destroy(pin_value *value);
int pin_value_equal(const pin_value *a, const pin_value *b);

#endif
