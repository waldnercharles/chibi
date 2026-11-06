#include "types/map.h"

#include "support/arena.h"
#include "support/xalloc.h"
#include "types/record.h"
#include "types/schema.h"
#include "types/text.h"
#include "types/trace.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool same_type(pin_value a, pin_value b)
{
    if (a.type != b.type || a.container != b.container ||
        !pin_record_same(a.record, b.record) ||
        strcmp(a.type_name ? a.type_name : "", b.type_name ? b.type_name : "")) {
        return false;
    }
    if (a.type != PIN_STRUCT || a.record || (!a.schema && !b.schema)) {
        return true;
    }
    const type_desc *expected = pin_value_type(&a), *actual = pin_value_type(&b);
    uint64_t left = a.schema ? pin_schema_layout(a.schema, a.type_name)
                             : type_layout_hash(expected);
    uint64_t right = b.schema ? pin_schema_layout(b.schema, b.type_name)
                              : type_layout_hash(actual);
    return expected && actual && expected->size == actual->size && left && left == right;
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

/* Below this many entries a scan beats hashing, as for indexed arrays. */
enum
{
    MAP_INDEX_MIN_COUNT = 8,
    MAP_LOAD_PERCENT = 75
};
/* A slot mask is 32 bits wide. */
static const uint64_t MAP_MAX_SLOTS = UINT64_C(1) << 31;
/* Equal keys hash equally: 0.0 and -0.0 share a hash, and a string hashes its
 * bytes. The finalizer is splitmix64's. */
static uint32_t key_hash(const pin_value *key)
{
    uint64_t bits = 0;
    switch (key->type) {
        case PIN_BOOL: bits = key->b; break;
        case PIN_I8: bits = (uint64_t)key->i8; break;
        case PIN_U8: bits = key->u8; break;
        case PIN_I16: bits = (uint64_t)key->i16; break;
        case PIN_U16: bits = key->u16; break;
        case PIN_I32: bits = (uint64_t)key->i; break;
        case PIN_U32: bits = key->u; break;
        case PIN_I64: bits = (uint64_t)key->i64; break;
        case PIN_ENUM: bits = (uint64_t)key->e; break;
        case PIN_F32:
            if (key->f != 0) {
                memcpy(&bits, &key->f, sizeof key->f);
            }
            break;
        case PIN_F64:
            if (key->d != 0) {
                memcpy(&bits, &key->d, sizeof key->d);
            }
            break;
        case PIN_STR:
        case PIN_NAME:
            bits = 0xcbf29ce484222325u;
            {
                pin_string text = pin_text(*key);
                for (uint32_t i = 0; i < text.length; i++) {
                    bits = (bits ^ (unsigned char)text.bytes[i]) * 0x100000001b3u;
                }
            }
            break;
        default: bits = key->u64; break;
    }
    bits ^= bits >> 30;
    bits *= 0xbf58476d1ce4e5b9u;
    bits ^= bits >> 27;
    bits *= 0x94d049bb133111ebu;
    bits ^= bits >> 31;
    return (uint32_t)bits;
}
static uint32_t probe_distance(const pin_map *map, uint32_t hash, uint32_t slot)
{
    return (slot - (hash & map->slot_mask)) & map->slot_mask;
}
/* Robin Hood insertion: an entry farther from its home slot than the one
 * resting there takes the slot and carries the other on. */
static void index_insert(pin_map *map, uint32_t hash, int32_t entry)
{
    pin_map_slot carried = { hash, entry };
    uint32_t slot = hash & map->slot_mask, distance = 0;
    for (;;) {
        pin_map_slot *resting = &map->slots[slot];
        if (resting->entry < 0) {
            *resting = carried;
            return;
        }
        uint32_t resting_distance = probe_distance(map, resting->hash, slot);
        if (resting_distance < distance) {
            pin_map_slot swapped = *resting;
            *resting = carried;
            carried = swapped;
            distance = resting_distance;
        }
        slot = (slot + 1) & map->slot_mask;
        distance++;
    }
}
/* The slots of an index for `capacity` entries at most 75% full: a power of
 * two, 0 for a small map, or more than MAP_MAX_SLOTS when too many. */
static uint64_t slot_count(int capacity)
{
    if (capacity < MAP_INDEX_MIN_COUNT) {
        return 0;
    }
    uint64_t slots = MAP_INDEX_MIN_COUNT;
    while (slots <= MAP_MAX_SLOTS &&
           slots * MAP_LOAD_PERCENT < (uint64_t)capacity * 100) {
        slots *= 2;
    }
    return slots;
}
/* Indexes the map's entries in `slots`, which holds `count` slots. */
static void index_fill(pin_map *map, pin_map_slot *slots, uint64_t count)
{
    map->slots = slots;
    map->slot_mask = count ? (uint32_t)(count - 1) : 0;
    for (uint64_t i = 0; i < count; i++) {
        slots[i] = (pin_map_slot){ 0, -1 };
    }
    for (int i = 0; slots && i < map->count; i++) {
        index_insert(map, key_hash(&map->entries[i].key), i);
    }
}
/* An index sized for `capacity` entries, or none for a small map. */
static bool index_build(arena *storage, pin_map *map, int capacity)
{
    uint64_t count = storage ? slot_count(capacity) : 0;
    if (count > MAP_MAX_SLOTS) {
        return false;
    }
    pin_map_slot *slots = count ? arena_alloc(storage, (size_t)count * sizeof *slots)
                                : NULL;
    if (count && !slots) {
        return false;
    }
    index_fill(map, slots, count);
    return true;
}
size_t pin_map_index_bytes(int count)
{
    uint64_t slots = slot_count(count);
    return slots > MAP_MAX_SLOTS ? SIZE_MAX : (size_t)slots * sizeof(pin_map_slot);
}
void pin_map_index(pin_map *map, pin_map_slot *slots)
{
    assert(!map->slots && (slots != NULL) == (pin_map_index_bytes(map->count) > 0));
    index_fill(map, slots, slot_count(map->count));
}

int pin_map_find(const pin_map *map, const pin_value *key)
{
    if (!map || map->count < 0 || (map->count && !map->entries) || !key ||
        !pin_map_key_supported(*key) || !same_type(map->key_type, *key)) {
        return -1;
    }
    if (!map->slots) {
        for (int i = 0; i < map->count; i++) {
            if (pin_value_equal(&map->entries[i].key, key)) {
                return i;
            }
        }
        return -1;
    }
    uint32_t hash = key_hash(key), slot = hash & map->slot_mask;
    for (uint32_t distance = 0;; distance++) {
        const pin_map_slot *resting = &map->slots[slot];
        if (resting->entry < 0 || probe_distance(map, resting->hash, slot) < distance) {
            return -1;
        }
        if (resting->hash == hash &&
            pin_value_equal(&map->entries[resting->entry].key, key)) {
            return resting->entry;
        }
        slot = (slot + 1) & map->slot_mask;
    }
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
    if (map->slots) {
        mark(user, map->slots);
    }
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
    map->capacity = count;
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
    return index_build(storage, map, capacity) ? map : NULL;
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
            if (map->slots) {
                index_insert(map, key_hash(&entry->key), index);
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
        if (map->slots) {
            index_insert(map, key_hash(key), index);
        }
    }
    return pin_value_snapshot_to_arena(storage, &map->entries[index].value, value)
             ? NULL
             : map;
}
/* Removing keeps the others in order: the entries after it move down, its
 * slot empties by Robin Hood backward shift, and the slots naming later
 * entries follow them. Removal is linear in the map, but hashes nothing. */
static void remove_entry(pin_map *map, int index)
{
    memmove(
        map->entries + index,
        map->entries + index + 1,
        (size_t)(map->count - index - 1) * sizeof *map->entries
    );
    map->count--;
    if (!map->slots) {
        return;
    }
    uint32_t slot = 0;
    while (map->slots[slot].entry != index) {
        slot++;
    }
    for (uint32_t next = (slot + 1) & map->slot_mask;
         map->slots[next].entry >= 0 &&
         probe_distance(map, map->slots[next].hash, next) > 0;
         next = (next + 1) & map->slot_mask) {
        map->slots[slot] = map->slots[next];
        slot = next;
    }
    map->slots[slot] = (pin_map_slot){ 0, -1 };
    for (uint32_t i = 0; i <= map->slot_mask; i++) {
        map->slots[i].entry -= map->slots[i].entry > index;
    }
}
int pin_map_store_owned(arena *storage, pin_map **owner, const pin_value *key, const pin_value *value)
{
    pin_map *map = *owner;
    if (!storage || !map || !key || !value || !pin_map_key_supported(*key) ||
        !same_type(map->key_type, *key) || !same_type(map->value_type, *value)) {
        return -1;
    }
    int index = pin_map_find(map, key);
    if (index < 0 && map->count == map->capacity) {
        /* Doubling keeps appends amortized constant, as for local arrays. */
        if (map->capacity > INT32_MAX / 2) {
            return -1;
        }
        pin_map *grown = allocate(storage, map->capacity ? map->capacity * 2 : 4);
        if (!grown) {
            return -1;
        }
        grown->key_type = map->key_type;
        grown->value_type = map->value_type;
        grown->count = map->count;
        memcpy(grown->entries, map->entries, (size_t)map->count * sizeof *map->entries);
        if (!index_build(storage, grown, grown->capacity)) {
            return -1;
        }
        *owner = map = grown;
    }
    pin_value stored;
    if (pin_value_snapshot_to_arena(storage, &stored, value)) {
        return -1;
    }
    if (index < 0) {
        index = map->count;
        if (pin_value_snapshot_to_arena(storage, &map->entries[index].key, key)) {
            return -1;
        }
        map->count++;
        if (map->slots) {
            index_insert(map, key_hash(key), index);
        } else if (map->count >= MAP_INDEX_MIN_COUNT && !index_build(storage, map, map->capacity)) {
            return -1;
        }
    }
    map->entries[index].value = stored;
    return 0;
}
int pin_map_delete_owned(arena *storage, pin_map *map, const pin_value *key)
{
    if (!storage || !map || !key || !pin_map_key_supported(*key) ||
        !same_type(map->key_type, *key)) {
        return -1;
    }
    int index = pin_map_find(map, key);
    if (index >= 0) {
        remove_entry(map, index);
    }
    return 0;
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
        remove_entry(map, index);
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
