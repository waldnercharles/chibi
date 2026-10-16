#include "language/semantic_internal.h"
#include "types/scalar.h"

#include <math.h>
#include <string.h>

/* A total budget bounds repeated constant dependencies, in addition to depth.
 * Check the complete admissibility graph before short-circuit evaluation. */
#define CONSTANT_STEPS 65536

static bool convert(pin_value *value, pin_type type)
{
    if (value->type == type) {
        return true;
    }
    if (behavior_scalar_string(value->type) && behavior_scalar_string(type)) {
        value->type = type;
        return true;
    }
    if (value->type == PIN_F32 && type == PIN_F64) {
        *value = (pin_value){ .type = PIN_F64, .d = value->f };
        return true;
    }
    if (type == PIN_F32) {
        if (value->type != PIN_F64) {
            if (!behavior_scalar_cast_supported(value->type, PIN_F64)) {
                return false;
            }
            *value = behavior_scalar_cast(*value, PIN_F64);
        }
        *value = (pin_value){ .type = PIN_F32, .f = (float)value->d };
        return true;
    }
    if (!behavior_scalar_cast_supported(value->type, type)) {
        return false;
    }
    *value = behavior_scalar_cast(*value, type);
    return true;
}
static const char *operation(script_operator op, pin_value left, pin_value right, pin_type type, pin_value *out)
{
    *out = (pin_value){ .type = type };
    if (op == SCRIPT_OP_NONE) {
        *out = left;
        return NULL;
    }
    if (op == SCRIPT_OP_NOT && left.type == PIN_BOOL) {
        out->b = !left.b;
        return NULL;
    }
    if (op == SCRIPT_OP_EQ || op == SCRIPT_OP_NE) {
        bool equal;
        if (behavior_scalar_string(left.type)) {
            equal = behavior_string_equal(left.str, right.str);
        } else if (left.type == PIN_F32) {
            equal = left.f == right.f;
        } else if (left.type == PIN_F64) {
            equal = left.d == right.d;
        } else {
            equal = behavior_scalar_bits(left) == behavior_scalar_bits(right);
        }
        out->b = op == SCRIPT_OP_EQ ? equal : !equal;
        return NULL;
    }
    if (op >= SCRIPT_OP_BIT_AND && op <= SCRIPT_OP_BIT_NOT &&
        left.type >= PIN_I8 && left.type <= PIN_U64) {
        unsigned width = (unsigned)behavior_scalar_size(left.type) * CHAR_BIT;
        uint64_t mask = UINT64_MAX >> (64 - width);
        uint64_t x = behavior_scalar_bits(left) & mask;
        uint64_t y = op == SCRIPT_OP_BIT_NOT ? 0 : behavior_scalar_bits(right);
        unsigned shift = (unsigned)(y & (width - 1));
        uint64_t bits = 0;
        switch (op) {
            case SCRIPT_OP_BIT_AND: bits = x & y; break;
            case SCRIPT_OP_BIT_OR: bits = x | y; break;
            case SCRIPT_OP_BIT_XOR: bits = x ^ y; break;
            case SCRIPT_OP_BIT_NOT: bits = ~x; break;
            case SCRIPT_OP_SHIFT_LEFT: bits = x << shift; break;
            case SCRIPT_OP_SHIFT_RIGHT:
                bits = x >> shift;
                if (shift && behavior_scalar_signed(left.type) &&
                    (x & (UINT64_C(1) << (width - 1)))) {
                    bits |= mask ^ (mask >> shift);
                }
                break;
            default: break;
        }
        *out = behavior_scalar_from_bits(type, bits & mask);
        return NULL;
    }
    if (left.type == PIN_I32 || left.type == PIN_I64) {
        int64_t x = left.type == PIN_I32 ? left.i : left.i64;
        int64_t y = right.type == PIN_I32 ? right.i : right.i64;
        int64_t minimum = left.type == PIN_I32 ? INT32_MIN : INT64_MIN;
        if ((op == SCRIPT_OP_DIV || op == SCRIPT_OP_MOD) &&
            (!y || (x == minimum && y == -1))) {
            return "invalid integer division or remainder";
        }
        uint64_t bits = 0;
        switch (op) {
            case SCRIPT_OP_ADD: bits = (uint64_t)x + (uint64_t)y; break;
            case SCRIPT_OP_SUB: bits = (uint64_t)x - (uint64_t)y; break;
            case SCRIPT_OP_MUL: bits = (uint64_t)x * (uint64_t)y; break;
            case SCRIPT_OP_DIV: bits = (uint64_t)(x / y); break;
            case SCRIPT_OP_MOD: bits = (uint64_t)(x % y); break;
            case SCRIPT_OP_NEG: bits = UINT64_C(0) - (uint64_t)x; break;
            case SCRIPT_OP_LT: out->b = x < y; return NULL;
            case SCRIPT_OP_LE: out->b = x <= y; return NULL;
            case SCRIPT_OP_GT: out->b = x > y; return NULL;
            case SCRIPT_OP_GE: out->b = x >= y; return NULL;
            default: return "unsupported scalar constant operator";
        }
        *out = behavior_scalar_from_bits(type, bits);
        return NULL;
    }
    if (left.type == PIN_U32 || left.type == PIN_U64) {
        uint64_t x = behavior_scalar_bits(left), y = behavior_scalar_bits(right);
        if (op != SCRIPT_OP_ADD && op != SCRIPT_OP_MUL) {
            return "unsupported unsigned constant operator";
        }
        *out = behavior_scalar_from_bits(type, op == SCRIPT_OP_ADD ? x + y : x * y);
        return NULL;
    }
    if (left.type == PIN_F32 || left.type == PIN_F64) {
        double x = left.type == PIN_F32 ? left.f : left.d;
        double y = right.type == PIN_F32 ? right.f : right.d, result;
        switch (op) {
            case SCRIPT_OP_ADD:
                result = left.type == PIN_F32 ? (float)(left.f + right.f) : x + y;
                break;
            case SCRIPT_OP_SUB:
                result = left.type == PIN_F32 ? (float)(left.f - right.f) : x - y;
                break;
            case SCRIPT_OP_MUL:
                result = left.type == PIN_F32 ? (float)(left.f * right.f) : x * y;
                break;
            case SCRIPT_OP_DIV:
                result = left.type == PIN_F32 ? (float)(left.f / right.f) : x / y;
                break;
            case SCRIPT_OP_NEG: result = -x; break;
            case SCRIPT_OP_LT: out->b = x < y; return NULL;
            case SCRIPT_OP_LE: out->b = x <= y; return NULL;
            case SCRIPT_OP_GT: out->b = x > y; return NULL;
            case SCRIPT_OP_GE: out->b = x >= y; return NULL;
            default: return "unsupported floating-point constant operator";
        }
        if (type == PIN_F32) {
            out->f = (float)result;
        } else {
            out->d = result;
        }
        return NULL;
    }
    return "unsupported scalar constant operator";
}
static const char *evaluate(const analyzer *a, int index, pin_value *out, int depth, int *steps)
{
    if (index < 0 || depth >= SCRIPT_MAX_DEPTH || --*steps < 0) {
        return "constant evaluation exceeds its bounded budget";
    }
    const script_expression *e = &a->out->expressions[index];
    if (e->type.container != PIN_CONTAINER_SINGLE ||
        (e->type.flags & ~(SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_LITERAL)) ||
        (e->type.storage < PIN_BOOL || e->type.storage > PIN_STR)) {
        return "requires a scalar compile-time value";
    }
    switch (e->kind) {
        case SCRIPT_EXPR_CONSTANT: *out = e->constant; return NULL;
        case SCRIPT_EXPR_SYMBOL:
            return evaluate(a, a->out->symbols[e->symbol].initializer, out, depth + 1, steps);
        case SCRIPT_EXPR_UNARY:
        case SCRIPT_EXPR_BINARY:
        case SCRIPT_EXPR_CAST: break;
        default: return "requires scalar compile-time values and operators";
    }
    pin_value left = { 0 }, right = { 0 };
    const char *error = evaluate(a, e->left, &left, depth + 1, steps);
    if (error) {
        return error;
    }
    if (e->kind == SCRIPT_EXPR_CAST) {
        if (!convert(&left, e->type.storage)) {
            return "unsupported scalar constant conversion";
        }
        *out = left;
        return NULL;
    }
    if (e->kind == SCRIPT_EXPR_BINARY) {
        if ((e->op == SCRIPT_OP_AND || e->op == SCRIPT_OP_OR) && left.type == PIN_BOOL) {
            if ((e->op == SCRIPT_OP_AND && !left.b) ||
                (e->op == SCRIPT_OP_OR && left.b)) {
                *out = left;
                return NULL;
            }
            return evaluate(a, e->right, out, depth + 1, steps);
        }
        error = evaluate(a, e->right, &right, depth + 1, steps);
        if (error) {
            return error;
        }
        script_type x = script_analyzer_expr_type(a, e->left),
                    y = script_analyzer_expr_type(a, e->right);
        if (script_type_numeric(x) && script_type_numeric(y)) {
            pin_type operand = script_analyzer_arithmetic_type(x, y).storage;
            if (!convert(&left, operand) || !convert(&right, operand)) {
                return "unsupported scalar constant conversion";
            }
        } else if (
            left.type != right.type && !(behavior_scalar_string(left.type) &&
                                         behavior_scalar_string(right.type))
        ) {
            return "incompatible scalar constant operands";
        }
    }
    return operation(e->op, left, right, e->type.storage, out);
}
const char *script_analyzer_scalar_constant(analyzer *a, int index, pin_type expected, pin_value *out)
{
    int steps = CONSTANT_STEPS;
    if (!script_analyzer_compile_time_value(a, index, &steps, true)) {
        return "requires bounded compile-time values and operators";
    }
    steps = CONSTANT_STEPS;
    pin_value result = { 0 };
    const char *error = evaluate(a, index, &result, 0, &steps);
    if (error) {
        return error;
    }
    if (!convert(&result, expected)) {
        return "incompatible scalar constant result";
    }
    *out = result;
    return NULL;
}
