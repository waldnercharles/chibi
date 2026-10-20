#ifndef CHIBI_VALUE_MAP_H
#define CHIBI_VALUE_MAP_H

#include "types/array.h"

typedef struct
{
    pin_value key, value;
} pin_map_entry;

/* Ordered associative values. Keys are immutable, and replacing a value keeps
 * its insertion position. Heap copies borrow struct payloads like pin_array;
 * arena snapshots own all managed key/value payloads. */
typedef struct pin_map
{
    pin_value key_type, value_type;
    int count;
    pin_map_entry *entries;
} pin_map;

bool pin_map_key_supported(pin_value);
bool pin_map_entry_valid(const pin_map *, const pin_value *key, const pin_value *value);
void pin_map_trace_storage(arena *, pin_map *);
int pin_map_find(const pin_map *, const pin_value *key);
pin_map *pin_map_clone(const pin_map *);
pin_map *pin_map_copy_to_arena(arena *, const pin_map *);
pin_map *pin_map_snapshot_to_arena(arena *, const pin_map *);
pin_map *pin_map_create(arena *, pin_value key_type, pin_value value_type, const pin_map_entry *, int count);
pin_map *pin_map_set(arena *, const pin_map *, const pin_value *key, const pin_value *value);
pin_map *pin_map_remove(arena *, const pin_map *, const pin_value *key);
void pin_map_free(pin_map *);
int pin_map_equal(const pin_map *, const pin_map *);

#endif
