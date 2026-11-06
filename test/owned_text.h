#ifndef TEST_OWNED_TEXT_H
#define TEST_OWNED_TEXT_H

#include "support/arena.h"
#include "types/text.h"

/* Retained bytes can live outside the arena that owns their descriptor. */
static inline bool test_text_owned(const arena *storage, pin_value value)
{
    pin_string text = pin_text(value);
    if (!value.text) {
        return !text.length;
    }
    return arena_contains(storage, value.text) &&
           (!text.length || text.owner || arena_contains(storage, text.bytes));
}

#endif
