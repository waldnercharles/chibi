#include "types/union.h"

#include "support/xalloc.h"
#include "types/array.h"
#include "types/copy_internal.h"
#include "types/trace.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const pin_union cancelled = { .tag = PIN_UNION_CANCELLED_TAG };

void pin_union_cancelled(pin_value *out)
{
    *out = (pin_value){ .type = PIN_UNION, .ptr = (void *)&cancelled };
}

static pin_union *allocate(arena *storage, uint64_t tag, const pin_value *values, int count)
{
    assert(storage && tag && count >= 0 && (!count || values));
    assert((size_t)count <= (SIZE_MAX - sizeof(pin_union)) / sizeof(pin_value));
    pin_union *value = arena_alloc(storage, sizeof *value + (size_t)count * sizeof *values);
    *value = (pin_union){ .tag = tag, .count = count };
    memset(value->values, 0, (size_t)count * sizeof *values);
    pin_arena_trace_union(storage, value);
    return value;
}
int pin_union_value(arena *storage, uint64_t tag, const pin_value *values, int count, pin_value *out)
{
    pin_union *value = allocate(storage, tag, values, count);
    for (int i = 0; i < count; i++) {
        if (pin_value_snapshot_to_arena(storage, &value->values[i], &values[i])) {
            return -1;
        }
    }
    *out = (pin_value){ .type = PIN_UNION, .ptr = value };
    return 0;
}
/* Heap-owned copies use one owner header and an arena for nested snapshots.
 * This follows pin_value_copy/destroy ownership, separate from arena values. */
typedef struct
{
    /* A 32-bit arena's natural alignment can be less than pin_value's. */
    _Alignas(pin_union) arena storage;
} union_owner;

int pin_union_copy(pin_value *out, const pin_value *source)
{
    *out = *source;
    pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
    const pin_union *from = source->ptr;
    if (!from || from == &cancelled) {
        return 0;
    }
    assert(from->count >= 0);
    assert(
        (size_t)from->count <=
        (SIZE_MAX - sizeof(union_owner) - sizeof(pin_union)) / sizeof(pin_value)
    );
    out->ptr = NULL;
    size_t bytes = sizeof(union_owner) + sizeof(pin_union) +
                   (size_t)from->count * sizeof(pin_value);
    union_owner *owner = xcalloc(1, bytes);
    pin_union *copy = (pin_union *)(owner + 1);
    *copy = (pin_union){ .tag = from->tag, .count = from->count };
    for (int i = 0; i < from->count; i++) {
        if (pin_value_snapshot_to_arena(&owner->storage, &copy->values[i], &from->values[i])) {
            arena_free(&owner->storage);
            free(owner);
            return -1;
        }
    }
    out->ptr = copy;
    return 0;
}

void pin_union_destroy(pin_union *value)
{
    if (!value || value == &cancelled) {
        return;
    }
    union_owner *owner = (union_owner *)value - 1;
    arena_free(&owner->storage);
    free(owner);
}
