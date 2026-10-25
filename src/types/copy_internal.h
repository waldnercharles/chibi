#ifndef CHIBI_TYPES_COPY_INTERNAL_H
#define CHIBI_TYPES_COPY_INTERNAL_H

#include "types/copy.h"

#include <stddef.h>

_Static_assert(sizeof(size_t) <= sizeof(uint64_t), "copy counters require 32/64-bit sizes");

extern _Thread_local pin_copy_scope *pin_copy_current;

typedef enum
{
    PIN_COPY_TEXT,
    PIN_COPY_NOMINAL,
    PIN_COPY_RECORD,
    PIN_COPY_VALUE_BUFFER,
    PIN_COPY_VALUE_HEADER,
    PIN_COPY_ARRAY_OWNER_SHELL,
    PIN_COPY_RECORD_CACHE_HIT,
    PIN_COPY_RECORD_FROZEN_CACHE_BYPASS,
    PIN_COPY_RECORD_DESCRIPTOR
} pin_copy_kind;

static inline void pin_copy_add(pin_copy_scope *scope, uint64_t *value, size_t amount)
{
    if (UINT64_MAX - *value < amount) {
        *value = UINT64_MAX;
        scope->counts.overflow = true;
        return;
    }
    *value += amount;
}

static inline void pin_copy_record(pin_copy_kind kind, size_t amount)
{
    pin_copy_scope *scope = pin_copy_current;
    if (!scope) {
        return;
    }
    uint64_t *value;
    switch (kind) {
        case PIN_COPY_ARRAY_OWNER_SHELL:
            value = &scope->counts.array_owner_shells;
            break;
        case PIN_COPY_RECORD_CACHE_HIT:
            value = &scope->counts.record_cache_hits;
            break;
        case PIN_COPY_RECORD_FROZEN_CACHE_BYPASS:
            value = &scope->counts.record_frozen_cache_bypasses;
            break;
        case PIN_COPY_RECORD_DESCRIPTOR:
            value = &scope->counts.record_descriptor_bytes;
            break;
        case PIN_COPY_TEXT: value = &scope->counts.text_bytes; break;
        case PIN_COPY_NOMINAL: value = &scope->counts.nominal_bytes; break;
        case PIN_COPY_RECORD: value = &scope->counts.record_bytes; break;
        case PIN_COPY_VALUE_BUFFER:
            value = &scope->counts.value_buffer_bytes;
            break;
        case PIN_COPY_VALUE_HEADER:
            value = &scope->counts.value_header_moves;
            break;
    }
    pin_copy_add(scope, value, amount);
}

static inline void pin_copy_array_allocation(bool adopting, size_t bytes)
{
    pin_copy_scope *scope = pin_copy_current;
    if (!scope) {
        return;
    }
    pin_array_copy_counts *counts = adopting ? &scope->counts.array_adoption
                                             : &scope->counts.array_detach;
    pin_copy_add(scope, &counts->allocations, 1);
    pin_copy_add(scope, &counts->allocated_bytes, bytes);
}
static inline void pin_copy_array_payload(bool adopting, bool packed, size_t amount)
{
    pin_copy_scope *scope = pin_copy_current;
    if (!scope) {
        return;
    }
    pin_array_copy_counts *counts = adopting ? &scope->counts.array_adoption
                                             : &scope->counts.array_detach;
    pin_copy_add(scope, packed ? &counts->packed_bytes : &counts->boxed_values, amount);
}

#endif
