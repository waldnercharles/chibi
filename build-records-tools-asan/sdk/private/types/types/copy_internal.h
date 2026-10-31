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
    PIN_COPY_VALUE_HEADER
} pin_copy_kind;

static inline void pin_copy_record(pin_copy_kind kind, size_t amount)
{
    pin_copy_scope *scope = pin_copy_current;
    if (!scope) {
        return;
    }
    uint64_t *value;
    switch (kind) {
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
    if (UINT64_MAX - *value < amount) {
        *value = UINT64_MAX;
        scope->counts.overflow = true;
        return;
    }
    *value += amount;
}

#endif
