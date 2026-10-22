#include "runtime/arithmetic.h"

#include "execution/scalar.h"
#include "support/arena.h"
#include "types/array.h"
#include "types/catalog.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>
enum
{
    GENERIC_ADD,
    GENERIC_SUB,
    GENERIC_MUL,
    GENERIC_DIV,
    GENERIC_MIN,
    GENERIC_MAX,
    GENERIC_MOD
};

pin_value native_arithmetic_literal_identity(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    (void)a;
    return inputs[0];
}

static pin_value numeric_op(pin_value a, pin_value b, int operation)
{
    if (a.type >= PIN_I8 && a.type <= PIN_U64 && b.type >= PIN_I8 && b.type <= PIN_U64) {
        const behavior_function_op ops[] = { BF_ADD, BF_SUB, BF_MUL, BF_DIV,
                                             BF_MIN, BF_MAX, BF_MOD };
        pin_value out = a;
        return behavior_scalar_integer_op(ops[operation], a, b, &out, NULL)
                 ? out
                 : (pin_value){ .type = PIN_FLOW };
    }
    if (a.type == PIN_F32 && b.type == PIN_F64) {
        bool subnormal = (a.f != 0.0f && fabsf(a.f) < FLT_MIN) ||
                         (b.d != 0.0 && fabs(b.d) < FLT_MIN);
        if (subnormal) {
            a.type = PIN_F64;
            a.d = (double)a.f;
        } else {
            b.type = PIN_F32;
            b.f = (float)b.d;
        }
    } else if (a.type == PIN_F64 && b.type == PIN_F32) {
        b.type = PIN_F64;
        b.d = (double)b.f;
    }
    pin_value result = a;
    switch (a.type) {
        case PIN_F32:
            result.f = operation == GENERIC_MUL ? a.f * b.f
                     : operation == GENERIC_SUB ? a.f - b.f
                     : operation == GENERIC_DIV ? a.f / b.f
                     : operation == GENERIC_MIN ? (a.f < b.f ? a.f : b.f)
                     : operation == GENERIC_MAX ? (a.f > b.f ? a.f : b.f)
                     : operation == GENERIC_MOD ? fmodf(a.f, b.f)
                                                : a.f + b.f;
            break;
        case PIN_F64:
            result.d = operation == GENERIC_MUL ? a.d * b.d
                     : operation == GENERIC_SUB ? a.d - b.d
                     : operation == GENERIC_DIV ? a.d / b.d
                     : operation == GENERIC_MIN ? (a.d < b.d ? a.d : b.d)
                     : operation == GENERIC_MAX ? (a.d > b.d ? a.d : b.d)
                     : operation == GENERIC_MOD ? fmod(a.d, b.d)
                                                : a.d + b.d;
            break;
        case PIN_I32:
            if ((operation == GENERIC_DIV || operation == GENERIC_MOD) &&
                (b.i == 0 || (a.i == INT_MIN && b.i == -1))) {
                return (pin_value){ .type = PIN_FLOW };
            }
            result.i = operation == GENERIC_MUL
                         ? (int32_t)((uint32_t)a.i * (uint32_t)b.i)
                     : operation == GENERIC_SUB
                         ? (int32_t)((uint32_t)a.i - (uint32_t)b.i)
                     : operation == GENERIC_DIV ? a.i / b.i
                     : operation == GENERIC_MIN ? (a.i < b.i ? a.i : b.i)
                     : operation == GENERIC_MAX ? (a.i > b.i ? a.i : b.i)
                     : operation == GENERIC_MOD
                         ? a.i % b.i
                         : (int32_t)((uint32_t)a.i + (uint32_t)b.i);
            break;
        case PIN_I64:
            if ((operation == GENERIC_DIV || operation == GENERIC_MOD) &&
                (b.i64 == 0 || (a.i64 == INT64_MIN && b.i64 == -1))) {
                return (pin_value){ .type = PIN_FLOW };
            }
            result.i64 = operation == GENERIC_MUL
                           ? (int64_t)((uint64_t)a.i64 * (uint64_t)b.i64)
                       : operation == GENERIC_SUB
                           ? (int64_t)((uint64_t)a.i64 - (uint64_t)b.i64)
                       : operation == GENERIC_DIV ? a.i64 / b.i64
                       : operation == GENERIC_MIN ? (a.i64 < b.i64 ? a.i64 : b.i64)
                       : operation == GENERIC_MAX ? (a.i64 > b.i64 ? a.i64 : b.i64)
                       : operation == GENERIC_MOD
                           ? a.i64 % b.i64
                           : (int64_t)((uint64_t)a.i64 + (uint64_t)b.i64);
            break;
        default: result.type = PIN_FLOW; break;
    }
    return result;
}

static const type_field *find_field(const type_desc *type, const char *name)
{
    for (int i = 0; i < type->field_count; i++) {
        if (!strcmp(type->fields[i].name, name)) {
            return &type->fields[i];
        }
    }
    return NULL;
}

/* The scalar a field of type `field` combines with: an integer becomes a
 * double for a floating field, as integers widen implicitly to floating
 * types, and numeric_op reads both operands as floating. */
static pin_value field_scalar(pin_value scalar, pin_type field)
{
    if ((field != PIN_F32 && field != PIN_F64) || scalar.type < PIN_I8 ||
        scalar.type > PIN_U64) {
        return scalar;
    }
    pin_value widened;
    behavior_scalar_cast(scalar, PIN_F64, BEHAVIOR_CAST_EXACT, &widened);
    return widened;
}

static pin_value struct_binary(pin_value aggregate, pin_value other, int operation, arena *a, int aggregate_left)
{
    const type_desc *type = type_catalog_find(aggregate.type_name);
    const type_desc *other_type = other.type == PIN_STRUCT
                                    ? type_catalog_find(other.type_name)
                                    : NULL;
    if (!type || !type->field_count || !aggregate.ptr ||
        (other.type == PIN_STRUCT && !other_type) ||
        (other.type == PIN_STRUCT && !other.ptr)) {
        return (pin_value){ .type = PIN_FLOW };
    }
    void *memory = arena_alloc(a, type->size);
    if (!memory) {
        return (pin_value){ .type = PIN_STRUCT, .type_name = type->name };
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        pin_value left;
        pin_value right;
        if (pin_read_field(a, aggregate.ptr, field, aggregate_left ? &left : &right)) {
            return (pin_value){ .type = PIN_FLOW };
        }
        if (other.type == PIN_STRUCT) {
            const type_field *peer = find_field(other_type, field->name);
            if (!peer) {
                return (pin_value){ .type = PIN_FLOW };
            }
            if (pin_read_field(a, other.ptr, peer, aggregate_left ? &right : &left)) {
                return (pin_value){ .type = PIN_FLOW };
            }
        } else if (aggregate_left) {
            right = field_scalar(other, field->type);
        } else {
            left = field_scalar(other, field->type);
        }
        pin_value result = numeric_op(left, right, operation);
        if (result.type == PIN_FLOW) {
            return result;
        }
        if (field->type == PIN_F32 && result.type == PIN_F64) {
            result.type = PIN_F32;
            result.f = (float)result.d;
        }
        if (pin_write_field(a, memory, field, result)) {
            return (pin_value){ .type = PIN_FLOW };
        }
    }
    return (pin_value){ .type = PIN_STRUCT, .type_name = type->name, .ptr = memory };
}

static pin_value generic_binary(const pin_value *inputs, int input_count, arena *a, int operation)
{
    (void)input_count;
    pin_value left = inputs[0];
    pin_value right = inputs[1];
    if (left.type != PIN_STRUCT && right.type != PIN_STRUCT) {
        return numeric_op(left, right, operation);
    }
    return left.type == PIN_STRUCT ? struct_binary(left, right, operation, a, 1)
                                   : struct_binary(right, left, operation, a, 0);
}

pin_value native_arithmetic_add(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_ADD);
}

pin_value native_arithmetic_multiply(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_MUL);
}

pin_value native_arithmetic_subtract(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_SUB);
}

pin_value native_arithmetic_divide(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_DIV);
}

pin_value native_arithmetic_min(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_MIN);
}

pin_value native_arithmetic_max(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_MAX);
}

pin_value native_arithmetic_modulo(const pin_value *inputs, int input_count, arena *a)
{
    return generic_binary(inputs, input_count, a, GENERIC_MOD);
}

pin_value native_arithmetic_clamp(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    pin_value value = inputs[0];
    pin_value low = inputs[1];
    pin_value high = inputs[2];
    const pin_value pair[] = { value, high };
    pin_value bounded = generic_binary(pair, 2, a, GENERIC_MIN);
    const pin_value result[] = { bounded, low };
    return generic_binary(result, 2, a, GENERIC_MAX);
}

pin_value native_arithmetic_negate(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    (void)a;
    pin_value result = inputs[0];
    if (result.type >= PIN_I8 && result.type <= PIN_U64) {
        behavior_scalar_integer_op(BF_NEG, result, (pin_value){ 0 }, &result, NULL);
        return result;
    }
    switch (result.type) {
        case PIN_F32: result.f = -result.f; break;
        case PIN_F64: result.d = -result.d; break;
        case PIN_I32:
            result.i = (int32_t)((uint32_t)0 - (uint32_t)result.i);
            break;
        case PIN_I64:
            result.i64 = (int64_t)((uint64_t)0 - (uint64_t)result.i64);
            break;
        default: result.type = PIN_FLOW; break;
    }
    return result;
}

pin_value native_arithmetic_abs(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    (void)a;
    pin_value result = inputs[0];
    if (result.type >= PIN_I8 && result.type <= PIN_U64) {
        behavior_scalar_integer_op(BF_ABS, result, (pin_value){ 0 }, &result, NULL);
        return result;
    }
    switch (result.type) {
        case PIN_F32: result.f = fabsf(result.f); break;
        case PIN_F64: result.d = fabs(result.d); break;
        case PIN_I32:
            result.i = result.i < 0 ? (int32_t)((uint32_t)0 - (uint32_t)result.i)
                                    : result.i;
            break;
        case PIN_I64:
            result.i64 = result.i64 < 0
                           ? (int64_t)((uint64_t)0 - (uint64_t)result.i64)
                           : result.i64;
            break;
        default: result.type = PIN_FLOW; break;
    }
    return result;
}

#define CALL(KEY, KERNEL, NAME, SOURCE, OPERATOR, INPUTS, OUTPUTS, AGGREGATE)                               \
    static const char *KEY##_call(arena *storage, const pin_value *arguments, int count, pin_value *result) \
    {                                                                                                       \
        pin_value value = native_arithmetic_##KERNEL(arguments, count, storage);                            \
        if (value.type == PIN_FLOW ||                                                                       \
            (value.type == PIN_STRUCT &&                                                                    \
             value.container == PIN_CONTAINER_SINGLE && !value.ptr))                                        \
            return "Native value operation failed";                                                         \
        return pin_value_snapshot_to_arena(storage, result, &value)                                         \
                 ? "Cannot retain native value result"                                                      \
                 : NULL;                                                                                    \
    }                                                                                                       \
    const node_host_call NATIVE_ARITHMETIC_##KEY##_CALL = { .value = KEY##_call };
NATIVE_ARITHMETIC_OPERATIONS(CALL)
#undef CALL
const node_access NATIVE_ARITHMETIC_ACCESS = { .flags = NODE_EFFECT_PURE };
