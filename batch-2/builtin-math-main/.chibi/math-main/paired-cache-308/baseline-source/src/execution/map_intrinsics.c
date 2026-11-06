#include "execution/map_intrinsics.h"

#include "support/array.h"
#include "types/map.h"
#include "types/record.h"

#include <assert.h>
#include <string.h>

static bool result_type(pin_value expected, pin_value value)
{
    return expected.type == value.type && expected.container == value.container &&
           pin_record_same(expected.record, value.record) &&
           !strcmp(
               expected.type_name ? expected.type_name : "",
               value.type_name ? value.type_name : ""
           );
}

static const char *make_map(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count >= 3 && (count & 1));
    pin_map_entry *entries = NULL;
    for (int i = 3; i < count; i += 2) {
        apush(entries, ((pin_map_entry){ args[i], args[i + 1] }));
    }
    pin_map *map = pin_map_create(storage, args[1], args[2], entries, acount(entries));
    afree(entries);
    if (!map) {
        return "Invalid map key, value, or retained payload";
    }
    *out = args[0];
    out->ptr = map;
    return NULL;
}
/* An absent key reads the optional third argument, the value type's zero. */
static const char *get_map(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2 || count == 3);
    const pin_map *map = args[0].ptr;
    int index = pin_map_find(map, &args[1]);
    if (index < 0 && count == 2) {
        return "Map key is absent or invalid";
    }
    const pin_value *value = index < 0 ? &args[2] : &map->entries[index].value;
    if (!result_type(*out, *value)) {
        return "Map value differs from its declared result type";
    }
    return pin_value_snapshot_to_arena(storage, out, value)
             ? "Cannot retain map value"
             : NULL;
}
static const char *has_map(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)storage;
    (void)count;
    if (!pin_map_key_supported(args[1])) {
        return "Invalid or nonfinite map key";
    }
    *out = (pin_value){ .type = PIN_BOOL,
                        .b = pin_map_find(args[0].ptr, &args[1]) >= 0 };
    return NULL;
}
static const char *remove_map(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)count;
    pin_map *map = pin_map_remove(storage, args[0].ptr, &args[1]);
    if (!map) {
        return "Invalid map removal key or retained payload";
    }
    *out = args[0];
    out->ptr = map;
    return NULL;
}
/* Ordinals belong only to the insertion-order iterator, never source indexing. */
static const char *item_map(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 3);
    (void)count;
    const pin_map *map = args[0].ptr;
    int index = args[1].i;
    if (!map || index < 0 || index >= map->count) {
        return "Map iterator is outside its snapshot";
    }
    const pin_value *value = args[2].b ? &map->entries[index].key
                                       : &map->entries[index].value;
    if (!result_type(*out, *value)) {
        return "Map iterator item differs from its declared result type";
    }
    return pin_value_snapshot_to_arena(storage, out, value)
             ? "Cannot retain map iterator item"
             : NULL;
}

/* `m[k] = v` and a removal on a local's own map, in place. */
static const char *store_map(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 3);
    (void)count;
    pin_value *header = (pin_value *)(uintptr_t)args[0].u64;
    pin_map *map = header->ptr;
    if (pin_map_store_owned(storage, &map, &args[1], &args[2])) {
        return "Invalid map key, value, or retained payload";
    }
    header->ptr = map;
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return NULL;
}
static const char *delete_map(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)count;
    const pin_value *header = (const pin_value *)(uintptr_t)args[0].u64;
    if (pin_map_delete_owned(storage, header->ptr, &args[1])) {
        return "Invalid map removal key";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return NULL;
}

const node_host_call EXECUTION_map_make_call = { .value = make_map };
const node_host_call EXECUTION_map_get_call = { .value = get_map };
const node_host_call EXECUTION_map_has_call = { .value = has_map };
const node_host_call EXECUTION_map_remove_call = { .value = remove_map };
const node_host_call EXECUTION_map_item_call = { .value = item_map };
const node_host_call EXECUTION_map_store_call = { .value = store_map };
const node_host_call EXECUTION_map_delete_call = { .value = delete_map };
