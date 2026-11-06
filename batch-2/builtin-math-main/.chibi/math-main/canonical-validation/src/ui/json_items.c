#include "ui/json_items.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <stdint.h>
#include <stdlib.h>

/* Items before this index cost less to walk to than to list. */
#define WALKED_ITEMS 8
#define FIRST_CAPACITY 64

typedef struct listed
{
    yyjson_mut_val *array; /* NULL for a free slot */
    yyjson_mut_val *last;  /* the array's last item when listed */
    size_t size;
    yyjson_mut_val **items; /* array.h vector, kept for reuse after a clear */
} listed;

/* An open-addressed table of listed arrays, at most half full. */
struct json_items
{
    listed *slots;
    size_t capacity, used;
};

json_items *json_items_new()
{
    return xcalloc(1, sizeof(json_items));
}

void json_items_free(json_items *items)
{
    for (size_t i = 0; i < items->capacity; i++) {
        afree(items->slots[i].items);
    }
    free(items->slots);
    free(items);
}

void json_items_clear(json_items *items)
{
    for (size_t i = 0; i < items->capacity; i++) {
        items->slots[i].array = NULL;
    }
    items->used = 0;
}

static size_t slot_of(const json_items *items, const yyjson_mut_val *array)
{
    uint64_t hash = (uint64_t)(uintptr_t)array * 0x9e3779b97f4a7c15ull;
    size_t i = (size_t)(hash >> 32) & (items->capacity - 1);
    while (items->slots[i].array && items->slots[i].array != array) {
        i = (i + 1) & (items->capacity - 1);
    }
    return i;
}

static void grow(json_items *items)
{
    listed *old = items->slots;
    size_t old_capacity = items->capacity;
    items->capacity = old_capacity ? old_capacity * 2 : FIRST_CAPACITY;
    items->slots = xcalloc(items->capacity, sizeof *items->slots);
    for (size_t i = 0; i < old_capacity; i++) {
        if (old[i].array) {
            items->slots[slot_of(items, old[i].array)] = old[i];
        } else {
            afree(old[i].items);
        }
    }
    free(old);
}

static listed *find(json_items *items, yyjson_mut_val *array)
{
    if ((items->used + 1) * 2 > items->capacity) {
        grow(items);
    }
    listed *slot = &items->slots[slot_of(items, array)];
    if (!slot->array) {
        slot->array = array;
        slot->last = NULL;
        items->used++;
    }
    return slot;
}

static void list(listed *slot, size_t size)
{
    asetlen(slot->items, (int)size);
    yyjson_mut_val *item;
    yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(slot->array);
    for (size_t i = 0; (item = yyjson_mut_arr_iter_next(&iter)); i++) {
        slot->items[i] = item;
    }
    slot->size = size;
    slot->last = yyjson_mut_arr_get_last(slot->array);
}

yyjson_mut_val *json_items_get(json_items *items, yyjson_mut_val *array, size_t index)
{
    size_t size = yyjson_mut_arr_size(array);
    if (index >= size) {
        return NULL;
    }
    if (index < WALKED_ITEMS) {
        return yyjson_mut_arr_get(array, index);
    }
    listed *slot = find(items, array);
    if (slot->size != size || slot->last != yyjson_mut_arr_get_last(array)) {
        list(slot, size);
    }
    return slot->items[index];
}
