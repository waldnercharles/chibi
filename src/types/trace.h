#ifndef CHIBI_VALUE_TRACE_H
#define CHIBI_VALUE_TRACE_H
#include "support/arena.h"
#include "types/array.h"
#include "types/pointer.h"
#include "types/union.h"

/* Whether a value contains a managed pointer, including inside aggregate
 * payloads. Does not follow pointees. Unavailable layouts/excessive depth fail
 * closed so native owners cannot retain an unrooted managed edge. */
bool pin_value_has_managed_pointer(const pin_value *value);
/* Distinguishes an unsupported pointer from an unavailable/malformed layout,
 * preserving existing value-retention diagnostics at native boundaries. */
const char *pin_value_managed_boundary_error(const pin_value *value);

/* Trace only declared managed fields; scalar words and padding are not scanned. */
void pin_value_trace(const pin_value *value, arena_marker mark, void *user);
/* Whether an array's items can reach storage of their own: packed scalars and
 * plain records reach nothing beyond the array's payload. */
bool pin_array_items_reach(const pin_array *array);
/* Traces an owner array's items as pin_value_trace traces each of them. */
void pin_array_trace_items(const pin_array *array, arena_marker mark, void *user);
void pin_arena_trace_struct(arena *storage, void *data, const type_desc *type);
void pin_arena_trace_array(arena *storage, pin_array *array);
void pin_arena_trace_union(arena *storage, pin_union *value);
void pin_arena_trace_pointer(arena *storage, pin_pointer *value);
#endif
