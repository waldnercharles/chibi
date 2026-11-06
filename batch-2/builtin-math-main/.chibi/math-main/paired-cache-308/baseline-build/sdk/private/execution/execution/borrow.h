#ifndef CHIBI_EXECUTION_BORROW_H
#define CHIBI_EXECUTION_BORROW_H

#include "execution/function.h"
#include "types/array.h"
#include "types/scalar.h"

#include <string.h>

typedef struct
{
    int variables, results, data, slots, locals, frames;
} behavior_borrow_layout;

static inline bool behavior_borrow_plain(pin_value value)
{
    return value.container == PIN_CONTAINER_SINGLE &&
           ((value.type >= PIN_BOOL && value.type <= PIN_F64) ||
            value.type == PIN_HANDLE || value.type == PIN_ENUM);
}

static inline bool behavior_borrow_name(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}

static inline bool behavior_borrow_overlap(const void *a, size_t bytes, const void *b, size_t count, size_t width)
{
    if (!a || !b || !bytes || !count) {
        return false;
    }
    if (count > SIZE_MAX / width) {
        return true;
    }
    uintptr_t left = (uintptr_t)a, right = (uintptr_t)b;
    return left <= right ? right - left < bytes : left - right < count * width;
}

static inline bool behavior_borrow_disjoint(
    const behavior_function_context *ctx,
    const void *address,
    size_t bytes,
    behavior_borrow_layout layout
)
{
    if (behavior_borrow_overlap(address, bytes, ctx, 1, sizeof *ctx) ||
        behavior_borrow_overlap(address, bytes, ctx->variables, (size_t)layout.variables, sizeof(pin_value)) ||
        behavior_borrow_overlap(address, bytes, ctx->results, (size_t)layout.results, sizeof(pin_value)) ||
        behavior_borrow_overlap(
            address,
            bytes,
            ctx->data ? ctx->data->values : NULL,
            (size_t)layout.data,
            sizeof(pin_value)
        )) {
        return false;
    }
    const behavior_function_activation *a = ctx->activation;
    if (!a) {
        return true;
    }
    uint64_t saved = (uint64_t)layout.slots + (uint64_t)layout.locals +
                     (uint64_t)layout.frames;
    if (saved > SIZE_MAX) {
        return false;
    }
    return !behavior_borrow_overlap(address, bytes, a, 1, sizeof *a) &&
           !behavior_borrow_overlap(
               address,
               bytes,
               a->values,
               (size_t)saved,
               sizeof *a->values
           ) &&
           !behavior_borrow_overlap(
               address,
               bytes,
               a->wake_times,
               (size_t)layout.frames,
               sizeof *a->wake_times
           );
}

/* The compiler proves storage is stable and unobserved for this invocation.
 * Native inputs can still carry inconsistent item headers: preserve their
 * canonical deep-copy behavior instead of trusting the declared array type. */
static inline bool behavior_borrow_array(
    const behavior_function_context *ctx,
    behavior_borrow_layout layout,
    const pin_value *value,
    pin_value *out
)
{
    pin_value element = *value;
    element.container = PIN_CONTAINER_SINGLE;
    if (value->container != PIN_CONTAINER_ARRAY || !behavior_borrow_plain(element)) {
        return false;
    }
    const pin_array *array = value->ptr;
    if (array) {
        /* Generated C uses this guard without linking the owned-value runtime.
         * Only plain scalar elements reach it, so layout validation needs no
         * record/catalog lookup or generic array helper. */
        size_t width = array->stride ? array->stride : sizeof(pin_value);
        if (array->count < 0 || array->capacity < array->count ||
            array->record || array->element_type != value->type ||
            (size_t)array->capacity > SIZE_MAX / width ||
            (array->stride
                 ? (array->items || array->stride != behavior_scalar_size(value->type) ||
                    (array->count && !array->data) ||
                    (array->type_name && !strncmp(array->type_name, "__collection_", 13)))
                 : (array->data || (array->count && !array->items))) ||
            !behavior_borrow_name(array->type_name, value->type_name) ||
            !behavior_borrow_disjoint(ctx, array, sizeof *array, layout) ||
            !behavior_borrow_disjoint(
                ctx,
                array->stride ? array->data : (void *)array->items,
                (size_t)array->capacity * width,
                layout
            )) {
            return false;
        }
        for (int j = 0; !array->stride && j < array->count; j++) {
            pin_value item = array->items[j];
            if (item.container != PIN_CONTAINER_SINGLE || item.type != value->type ||
                !behavior_borrow_name(item.type_name, value->type_name)) {
                return false;
            }
        }
    }
    *out = *value;
    return true;
}

const char *behavior_function_borrow_snapshot(behavior_function_context *, int);
bool behavior_function_borrow_argument(
    const behavior_function *,
    const behavior_function_context *,
    const pin_value *,
    pin_value *
);

#endif
