#include "types/map.h"

#include "support/arena.h"
#include "support/xalloc.h"
#include "types/record.h"
#include "types/trace.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool same_type(pin_value a, pin_value b)
{
    return a.type == b.type && a.container == b.container &&
           pin_record_same(a.record, b.record) &&
           !strcmp(a.type_name ? a.type_name : "", b.type_name ? b.type_name : "");
}

bool pin_map_key_supported(pin_value value)
{
    if (value.container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    if (value.type == PIN_F32) {
        return isfinite(value.f);
    }
    if (value.type == PIN_F64) {
        return isfinite(value.d);
    }
    return value.type == PIN_BOOL ||
           (value.type >= PIN_I8 && value.type <= PIN_U64) ||
           value.type == PIN_STR || value.type == PIN_NAME ||
           value.type == PIN_ENUM || value.type == PIN_HANDLE;
}

int pin_map_find(const pin_map *map, const pin_value *key)
{
    if (!map || map->count < 0 || (map->count && !map->entries) || !key ||
        !pin_map_key_supported(*key) || !same_type(map->key_type, *key)) {
        return -1;
    }
    for (int i = 0; i < map->count; i++) {
        if (pin_value_equal(&map->entries[i].key, key)) {
            return i;
        }
    }
    return -1;
}
bool pin_map_entry_valid(const pin_map *map, const pin_value *key, const pin_value *value)
{
    return map && key && value && pin_map_key_supported(*key) &&
           same_type(map->key_type, *key) && same_type(map->value_type, *value);
}

static void trace_map(const void *data, const void *, arena_marker mark, void *user)
{
    const pin_map *map = data;
    pin_value_trace(&map->key_type, mark, user);
    pin_value_trace(&map->value_type, mark, user);
    mark(user, map->entries);
    for (int i = 0; i < map->count; i++) {
        pin_value_trace(&map->entries[i].key, mark, user);
        pin_value_trace(&map->entries[i].value, mark, user);
    }
}
void pin_map_trace_storage(arena *storage, pin_map *map)
{
    arena_set_trace(storage, map, trace_map, NULL);
}

static pin_map *allocate(arena *storage, int count)
{
    if (count < 0 ||
        (size_t)count > (SIZE_MAX - sizeof(pin_map)) / sizeof(pin_map_entry)) {
        return NULL;
    }
    size_t size = sizeof(pin_map) + (size_t)count * sizeof(pin_map_entry);
    pin_map *map = storage ? arena_alloc(storage, size) : xcalloc(1, size);
    if (!map) {
        return NULL;
    }
    memset(map, 0, size);
    map->entries = (pin_map_entry *)(map + 1);
    if (storage) {
        pin_map_trace_storage(storage, map);
    }
    return map;
}

static pin_value shape(pin_value value)
{
    value.u64 = 0;
    return value;
}

static int copy_value(arena *storage, bool snapshot, pin_value *out, const pin_value *in)
{
    if (!storage) {
        return pin_value_copy(out, in);
    }
    return snapshot ? pin_value_snapshot_to_arena(storage, out, in)
                    : pin_value_copy_to_arena(storage, out, in);
}

static pin_map *copy_map(arena *storage, const pin_map *source, bool snapshot, int capacity)
{
    if (!source || source->count < 0 || (source->count && !source->entries) ||
        capacity < source->count) {
        return NULL;
    }
    pin_map *map = allocate(storage, capacity);
    if (!map) {
        return NULL;
    }
    pin_value key_type = shape(source->key_type),
              value_type = shape(source->value_type);
    if (copy_value(storage, snapshot, &map->key_type, &key_type) ||
        copy_value(storage, snapshot, &map->value_type, &value_type)) {
        if (!storage) {
            pin_map_free(map);
        }
        return NULL;
    }
    for (int i = 0; i < source->count; i++) {
        map->count = i + 1;
        if (copy_value(
                storage,
                snapshot,
                &map->entries[i].key,
                &source->entries[i].key
            ) ||
            copy_value(
                storage,
                snapshot,
                &map->entries[i].value,
                &source->entries[i].value
            )) {
            if (!storage) {
                pin_map_free(map);
            }
            return NULL;
        }
    }
    return map;
}

pin_map *pin_map_clone(const pin_map *source)
{
    return source ? copy_map(NULL, source, false, source->count) : NULL;
}

pin_map *pin_map_copy_to_arena(arena *storage, const pin_map *source)
{
    return storage && source ? copy_map(storage, source, false, source->count) : NULL;
}

pin_map *pin_map_snapshot_to_arena(arena *storage, const pin_map *source)
{
    return storage && source ? copy_map(storage, source, true, source->count) : NULL;
}

pin_map *pin_map_create(
    arena *storage,
    pin_value key_type,
    pin_value value_type,
    const pin_map_entry *entries,
    int count
)
{
    if (!storage || count < 0 || (count && !entries) ||
        !pin_map_key_supported(shape(key_type))) {
        return NULL;
    }
    pin_map source = { .key_type = shape(key_type), .value_type = shape(value_type) };
    pin_map *map = copy_map(storage, &source, true, count);
    if (!map) {
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        const pin_map_entry *entry = &entries[i];
        if (!pin_map_key_supported(entry->key) ||
            !same_type(map->key_type, entry->key) ||
            !same_type(map->value_type, entry->value)) {
            return NULL;
        }
        int index = pin_map_find(map, &entry->key);
        if (index < 0) {
            index = map->count++;
            if (pin_value_snapshot_to_arena(
                    storage,
                    &map->entries[index].key,
                    &entry->key
                )) {
                return NULL;
            }
        }
        if (pin_value_snapshot_to_arena(
                storage,
                &map->entries[index].value,
                &entry->value
            )) {
            return NULL;
        }
    }
    return map;
}

pin_map *pin_map_set(arena *storage, const pin_map *source, const pin_value *key, const pin_value *value)
{
    if (!storage || !source || !key || !value || source->count == INT32_MAX ||
        !pin_map_key_supported(*key) || !same_type(source->key_type, *key) ||
        !same_type(source->value_type, *value)) {
        return NULL;
    }
    int index = pin_map_find(source, key);
    pin_map *map = copy_map(storage, source, true, source->count + (index < 0));
    if (!map) {
        return NULL;
    }
    if (index < 0) {
        index = map->count++;
        if (pin_value_snapshot_to_arena(storage, &map->entries[index].key, key)) {
            return NULL;
        }
    }
    return pin_value_snapshot_to_arena(storage, &map->entries[index].value, value)
             ? NULL
             : map;
}

pin_map *pin_map_remove(arena *storage, const pin_map *source, const pin_value *key)
{
    if (!storage || !source || !key || !pin_map_key_supported(*key) ||
        !same_type(source->key_type, *key)) {
        return NULL;
    }
    pin_map *map = copy_map(storage, source, true, source->count);
    if (!map) {
        return NULL;
    }
    int index = pin_map_find(map, key);
    if (index >= 0) {
        memmove(
            map->entries + index,
            map->entries + index + 1,
            (size_t)(map->count - index - 1) * sizeof *map->entries
        );
        map->count--;
    }
    return map;
}

void pin_map_free(pin_map *map)
{
    if (!map) {
        return;
    }
    pin_value_destroy(&map->key_type);
    pin_value_destroy(&map->value_type);
    for (int i = 0; i < map->count; i++) {
        pin_value_destroy(&map->entries[i].key);
        pin_value_destroy(&map->entries[i].value);
    }
    free(map);
}

int pin_map_equal(const pin_map *a, const pin_map *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->count != b->count || !same_type(a->key_type, b->key_type) ||
        !same_type(a->value_type, b->value_type)) {
        return 0;
    }
    for (int i = 0; i < a->count; i++) {
        int index = pin_map_find(b, &a->entries[i].key);
        if (index < 0 ||
            !pin_value_equal(&a->entries[i].value, &b->entries[index].value)) {
            return 0;
        }
    }
    return 1;
}
