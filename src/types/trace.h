#ifndef CHIBI_VALUE_TRACE_H
#define CHIBI_VALUE_TRACE_H
#include "support/arena.h"
#include "types/array.h"

/* Trace only declared managed fields; scalar words and padding are not scanned. */
void pin_value_trace(const pin_value *value, arena_marker mark, void *user);
void pin_arena_trace_struct(arena *storage, void *data, const type_desc *type);
void pin_arena_trace_array(arena *storage, pin_array *array);
#endif
