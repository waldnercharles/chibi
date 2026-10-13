#ifndef CHIBI_EXECUTION_BORROW_H
#define CHIBI_EXECUTION_BORROW_H

#include "execution/function.h"
#include "types/array.h"

#include <string.h>

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
    int variables,
    int results,
    int data
)
{
    return !behavior_borrow_overlap(address, bytes, ctx, 1, sizeof *ctx) &&
           !behavior_borrow_overlap(address, bytes, ctx->variables, (size_t)variables, sizeof(pin_value)) &&
           !behavior_borrow_overlap(address, bytes, ctx->results, (size_t)results, sizeof(pin_value)) &&
           !behavior_borrow_overlap(address, bytes, ctx->data->values, (size_t)data, sizeof(pin_value));
}

/* The compiler proves storage is stable and unobserved for this invocation.
 * Native inputs can still carry inconsistent item headers: preserve their
 * canonical deep-copy behavior instead of trusting the declared array type. */
static inline bool behavior_borrow_array(
    const behavior_function_context *ctx,
    int variables,
    int results,
    int data,
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
        if (array->count < 0 || (array->count && !array->items) ||
            (size_t)array->count > SIZE_MAX / sizeof(pin_value) ||
            array->element_type != value->type ||
            !behavior_borrow_name(array->type_name, value->type_name) ||
            !behavior_borrow_disjoint(ctx, array, sizeof *array, variables, results, data) ||
            !behavior_borrow_disjoint(
                ctx,
                array->items,
                (size_t)array->count * sizeof(pin_value),
                variables,
                results,
                data
            )) {
            return false;
        }
        for (int j = 0; j < array->count; j++) {
            const pin_value *item = &array->items[j];
            if (item->container != PIN_CONTAINER_SINGLE || item->type != value->type ||
                !behavior_borrow_name(item->type_name, value->type_name)) {
                return false;
            }
        }
    }
    *out = *value;
    return true;
}

const char *behavior_function_borrow_snapshot(behavior_function_context *, int);

#endif
