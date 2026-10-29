#ifndef CHIBI_VALUE_MAP_H
#define CHIBI_VALUE_MAP_H

#include "types/array.h"

#include <stdint.h>

typedef struct
{
    pin_value key, value;
} pin_map_entry;
/* One slot of a map's index: the entry it names, -1 when empty, and the key's
 * hash, so a probe compares keys only on a hash match. */
typedef struct
{
    uint32_t hash;
    int32_t entry;
} pin_map_slot;

/* Ordered associative values. Keys are immutable, and replacing a value keeps
 * its insertion position. Heap copies borrow struct payloads like pin_array;
 * arena snapshots own all managed key/value payloads.
 *
 * Entries stay dense in insertion order. An arena map of more than a few
 * entries also has an index, Odin's open-addressed Robin Hood table: a power
 * of two of slots, at most 75% full, each naming an entry. */
typedef struct pin_map_backing pin_map_backing;

typedef struct pin_map
{
    pin_value key_type, value_type;
    int count, capacity;
    pin_map_entry *entries;
    pin_map_slot *slots; /* NULL: lookups scan the entries. */
    uint32_t slot_mask;
    pin_map_backing *backing; /* Shared immutable entries until a write detaches. */
    arena *storage; /* Canonical managed arena for backed shells and payload metadata. */
} pin_map;

bool pin_map_key_supported(pin_value);
bool pin_map_entry_valid(const pin_map *, const pin_value *key, const pin_value *value);
void pin_map_trace_storage(arena *, pin_map *);
int pin_map_find(const pin_map *, const pin_value *key);
/* VALUE copies get a distinct shell sharing immutable backing. Mutation forks
 * nested value shells before changing an entry; pointers keep their identity. */
pin_map *pin_map_value_copy(arena *, const pin_map *);
int pin_map_make_unique(arena *, pin_map *);
void pin_map_release(pin_map *);
void pin_map_mutation_end(pin_map *);
pin_map *pin_map_clone(const pin_map *);
pin_map *pin_map_copy_to_arena(arena *, const pin_map *);
pin_map *pin_map_snapshot_to_arena(arena *, const pin_map *);
pin_map *pin_map_create(arena *, pin_value key_type, pin_value value_type, const pin_map_entry *, int count);
pin_map *pin_map_set(arena *, const pin_map *, const pin_value *key, const pin_value *value);
pin_map *pin_map_remove(arena *, const pin_map *, const pin_value *key);
/* Update an owning logical value. Shared backing detaches before the write;
 * nested collections become independent value shells. Legacy arena maps may move. */
int pin_map_store_owned(arena *, pin_map **, const pin_value *key, const pin_value *value);
int pin_map_delete_owned(arena *, pin_map *, const pin_value *key);
/* The bytes of the index a map read with `count` entries needs, 0 for a small
 * map, so a reader can charge them to its budget. */
size_t pin_map_index_bytes(int count);
/* Builds the index of a map read without one in `slots`, which holds
 * pin_map_index_bytes(map->count) bytes. */
void pin_map_index(pin_map *, pin_map_slot *slots);
void pin_map_free(pin_map *);
int pin_map_equal(const pin_map *, const pin_map *);

#endif
