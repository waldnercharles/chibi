#include "language/scalar_constant.h"

#include "language/numeric.h"
#include "language/semantic_internal.h"
#include "types/scalar.h"

#include <math.h>
#include <string.h>

static bool convert(arena *storage, pin_value *value, pin_type type, behavior_cast_mode mode)
{
    if (value->type == type) {
        return true;
    }
    if (behavior_scalar_string(value->type) && behavior_scalar_string(type)) {
        if (type == PIN_STR) {
            pin_string text = pin_text(*value);
            return !pin_value_text(storage, value, text.bytes, text.length);
        }
        const char *name = NULL;
        size_t nul = 0;
        if (pin_text_cstring(storage, *value, &name, &nul)) {
            return false;
        }
        size_t length = strlen(name) + 1;
        char *owned = arena_alloc(storage, length);
        memcpy(owned, name, length);
        *value = (pin_value){ .type = PIN_NAME, .str = owned };
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
            behavior_scalar_cast(*value, PIN_F64, BEHAVIOR_CAST_EXACT, value);
        }
        *value = (pin_value){ .type = PIN_F32, .f = (float)value->d };
        return true;
    }
    return behavior_scalar_cast_supported(value->type, type) &&
           behavior_scalar_cast(*value, type, mode, value);
}
const char *script_scalar_operation(
    script_operator op,
    pin_value left,
    pin_value right,
    pin_type type,
    pin_value *out
)
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
            equal = pin_text_equal(pin_text(left), pin_text(right));
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
        bool sign = behavior_scalar_signed(left.type) && (x >> (width - 1));
        bool past = y >= width;
        unsigned shift = past ? 0 : (unsigned)y;
        uint64_t bits = 0;
        switch (op) {
            case SCRIPT_OP_BIT_AND: bits = x & y; break;
            case SCRIPT_OP_BIT_OR: bits = x | y; break;
            case SCRIPT_OP_BIT_XOR: bits = x ^ y; break;
            case SCRIPT_OP_BIT_NOT: bits = ~x; break;
            case SCRIPT_OP_SHIFT_LEFT: bits = past ? 0 : x << shift; break;
            case SCRIPT_OP_SHIFT_RIGHT:
                bits = past ? 0 : x >> shift;
                if (sign) {
                    bits |= past ? mask : mask ^ (mask >> shift);
                }
                break;
            default: break;
        }
        *out = behavior_scalar_from_bits(type, bits & mask);
        return NULL;
    }
    if (left.type >= PIN_I8 && left.type <= PIN_U64 &&
        behavior_scalar_signed(left.type)) {
        int64_t x = (int64_t)behavior_scalar_bits(left);
        int64_t y = op == SCRIPT_OP_NEG ? 0 : (int64_t)behavior_scalar_bits(right);
        unsigned width = (unsigned)behavior_scalar_size(left.type) * CHAR_BIT;
        int64_t minimum = width == 64 ? INT64_MIN : -(INT64_C(1) << (width - 1));
        bool overflow = x == minimum && y == -1;
        uint64_t bits = 0;
        switch (op) {
            case SCRIPT_OP_ADD: bits = (uint64_t)x + (uint64_t)y; break;
            case SCRIPT_OP_SUB: bits = (uint64_t)x - (uint64_t)y; break;
            case SCRIPT_OP_MUL: bits = (uint64_t)x * (uint64_t)y; break;
            case SCRIPT_OP_DIV:
                bits = !y ? 0 : overflow ? (uint64_t)x : (uint64_t)(x / y);
                break;
            case SCRIPT_OP_MOD:
                bits = !y ? (uint64_t)x : overflow ? 0 : (uint64_t)(x % y);
                break;
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
    if (left.type >= PIN_I8 && left.type <= PIN_U64) {
        uint64_t x = behavior_scalar_bits(left),
                 y = op == SCRIPT_OP_NEG ? 0 : behavior_scalar_bits(right), bits;
        switch (op) {
            case SCRIPT_OP_ADD: bits = x + y; break;
            case SCRIPT_OP_SUB: bits = x - y; break;
            case SCRIPT_OP_MUL: bits = x * y; break;
            case SCRIPT_OP_DIV: bits = y ? x / y : 0; break;
            case SCRIPT_OP_MOD: bits = y ? x % y : x; break;
            case SCRIPT_OP_NEG: bits = UINT64_C(0) - x; break;
            case SCRIPT_OP_LT: out->b = x < y; return NULL;
            case SCRIPT_OP_LE: out->b = x <= y; return NULL;
            case SCRIPT_OP_GT: out->b = x > y; return NULL;
            case SCRIPT_OP_GE: out->b = x >= y; return NULL;
            default: return "unsupported unsigned constant operator";
        }
        *out = behavior_scalar_from_bits(type, bits);
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
            case SCRIPT_OP_MOD:
                result = left.type == PIN_F32 ? fmodf(left.f, right.f) : fmod(x, y);
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
static bool integer_type(script_type type)
{
    return type.container == PIN_CONTAINER_SINGLE && !type.nominal &&
           !(type.flags & ~(SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_LITERAL)) &&
           type.storage >= PIN_I8 && type.storage <= PIN_U64;
}
static bool integer_operator(script_operator op)
{
    switch (op) {
        case SCRIPT_OP_ADD:
        case SCRIPT_OP_SUB:
        case SCRIPT_OP_MUL:
        case SCRIPT_OP_DIV:
        case SCRIPT_OP_MOD:
        case SCRIPT_OP_BIT_AND:
        case SCRIPT_OP_BIT_OR:
        case SCRIPT_OP_BIT_XOR:
        case SCRIPT_OP_SHIFT_LEFT:
        case SCRIPT_OP_SHIFT_RIGHT: return true;
        default: return false;
    }
}
/* Named constants can share initializers, so the walk spends a step per
 * node and gives up past the evaluator's budget, as evaluate() does. */
static bool constant_at(const script_analysis *analysis, int index, int depth, int *steps)
{
    const script_expression *e = &analysis->expressions[index];
    if (depth >= SCRIPT_MAX_DEPTH || --*steps < 0 || !integer_type(e->type)) {
        return false;
    }
    switch (e->kind) {
        case SCRIPT_EXPR_CONSTANT:
            return e->constant.type >= PIN_I8 && e->constant.type <= PIN_U64;
        case SCRIPT_EXPR_SYMBOL: {
            const script_symbol *symbol = &analysis->symbols[e->symbol];
            return symbol->kind == SCRIPT_CONSTANT && symbol->initializer >= 0 &&
                   constant_at(analysis, symbol->initializer, depth + 1, steps);
        }
        case SCRIPT_EXPR_UNARY:
            return (e->op == SCRIPT_OP_NONE || e->op == SCRIPT_OP_NEG ||
                    e->op == SCRIPT_OP_BIT_NOT) &&
                   constant_at(analysis, e->left, depth + 1, steps);
        case SCRIPT_EXPR_BINARY:
            return integer_operator(e->op) &&
                   constant_at(analysis, e->left, depth + 1, steps) &&
                   constant_at(analysis, e->right, depth + 1, steps);
        case SCRIPT_EXPR_CAST:
            return constant_at(analysis, e->left, depth + 1, steps);
        default: return false;
    }
}
bool script_integer_constant(const script_analysis *analysis, int expression)
{
    int steps = SCRIPT_SCALAR_CONSTANT_STEPS;
    return constant_at(analysis, expression, 0, &steps);
}
typedef struct
{
    const script_analysis *analysis;
    script_numeric *numbers;
    script_numeric_error numeric;
    script_fold_error *error;
} folding;
static bool fold_failed(folding *f, script_fold_code code, int expression)
{
    *f->error = (script_fold_error){ code, expression };
    return false;
}
static bool fold_value(folding *f, int index, pin_value *out);
static bool unsigned_type(script_type type)
{
    return !(type.flags & SCRIPT_TYPE_UNTYPED) &&
           !behavior_scalar_signed(type.storage);
}
/* The exact value at index, which constant_at admits. A numeric failure
 * other than the reported ones is an intermediate beyond the exact limit. */
static const script_number *exact(folding *f, int index)
{
    const script_expression *e = &f->analysis->expressions[index];
    pin_value value;
    switch (e->kind) {
        case SCRIPT_EXPR_CONSTANT: value = e->constant; break;
        case SCRIPT_EXPR_SYMBOL:
            /* A named constant reports its own initializer. */
            if (!fold_value(f, f->analysis->symbols[e->symbol].initializer, &value)) {
                fold_failed(f, SCRIPT_FOLD_REPORTED, index);
                return NULL;
            }
            break;
        case SCRIPT_EXPR_CAST: {
            if (!fold_value(f, e->left, &value)) {
                return NULL;
            }
            pin_type to = e->type.storage;
            if (e->conversion == BEHAVIOR_CAST_WRAP) {
                value = behavior_scalar_from_bits(to, behavior_scalar_bits(value));
            } else if (!behavior_scalar_cast(value, to, e->conversion, &value)) {
                fold_failed(f, SCRIPT_FOLD_CONVERSION, index);
                return NULL;
            }
            break;
        }
        case SCRIPT_EXPR_UNARY: {
            const script_number *x = exact(f, e->left);
            if (!x || e->op == SCRIPT_OP_NONE) {
                return x;
            }
            if (e->op == SCRIPT_OP_NEG && unsigned_type(e->type)) {
                fold_failed(f, SCRIPT_FOLD_REPORTED, index);
                return NULL;
            }
            const script_number *result = NULL;
            if (e->op == SCRIPT_OP_BIT_NOT && unsigned_type(e->type)) {
                /* ~v keeps the type's width: max(T) - v. */
                pin_value high = behavior_scalar_from_bits(e->type.storage, UINT64_MAX);
                const script_number *top = script_number_scalar(f->numbers, high, &f->numeric);
                result = top
                           ? script_number_binary(f->numbers, SCRIPT_OP_SUB, top, x, &f->numeric)
                           : NULL;
            } else {
                result = script_number_unary(f->numbers, e->op, x, &f->numeric);
            }
            if (!result) {
                fold_failed(f, SCRIPT_FOLD_RANGE, index);
            }
            return result;
        }
        case SCRIPT_EXPR_BINARY: {
            const script_number *x = exact(f, e->left);
            const script_number *y = x ? exact(f, e->right) : NULL;
            if (!y) {
                return NULL;
            }
            const script_number *result = script_number_binary(f->numbers, e->op, x, y, &f->numeric);
            if (result) {
                return result;
            }
            switch (f->numeric.code) {
                case SCRIPT_NUMERIC_ZERO_DIVISOR:
                case SCRIPT_NUMERIC_NEGATIVE_SHIFT:
                    fold_failed(f, SCRIPT_FOLD_REPORTED, index);
                    break;
                case SCRIPT_NUMERIC_SHIFT_LIMIT:
                    fold_failed(f, SCRIPT_FOLD_SHIFT, e->right);
                    break;
                default: fold_failed(f, SCRIPT_FOLD_RANGE, index); break;
            }
            return NULL;
        }
        default: assert(false); return NULL;
    }
    const script_number *result = script_number_scalar(f->numbers, value, &f->numeric);
    if (!result) {
        fold_failed(f, SCRIPT_FOLD_RANGE, index);
    }
    return result;
}
/* The value at index, which must fit its type. */
static bool fold_value(folding *f, int index, pin_value *out)
{
    const script_number *value = exact(f, index);
    if (!value) {
        return false;
    }
    pin_type type = f->analysis->expressions[index].type.storage;
    return script_number_materialize(f->numbers, value, type, out, &f->numeric) ||
           fold_failed(f, SCRIPT_FOLD_RANGE, index);
}
bool script_integer_fold(const script_analysis *analysis, int expression, pin_value *out, script_fold_error *error)
{
    assert(script_integer_constant(analysis, expression));
    *error = (script_fold_error){ SCRIPT_FOLD_OK, -1 };
    folding f = { .analysis = analysis, .error = error };
    f.numbers = script_numeric_new(NULL, &f.numeric);
    assert(f.numbers);
    bool folded = fold_value(&f, expression, out);
    script_numeric_free(f.numbers);
    return folded;
}
/* What evaluating an integer constant that cannot fold reports. */
static const char *fold_message(script_fold_error failure)
{
    switch (failure.code) {
        case SCRIPT_FOLD_SHIFT:
            return "integer constant shift count is too large";
        case SCRIPT_FOLD_CONVERSION:
            return "integer constant does not fit its conversion";
        case SCRIPT_FOLD_REPORTED:
            return "integer constant divides by zero, shifts by a negative "
                   "count or negates an unsigned value";
        default: return "integer constant does not fit its type";
    }
}
static const char *evaluate(
    arena *storage,
    const script_analysis *analysis,
    int index,
    pin_value *out,
    int depth,
    int *steps
)
{
    if (index < 0 || depth >= SCRIPT_MAX_DEPTH || --*steps < 0) {
        return "constant evaluation exceeds its bounded budget";
    }
    const script_expression *e = &analysis->expressions[index];
    if (e->type.container != PIN_CONTAINER_SINGLE ||
        (e->type.flags & ~(SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_LITERAL)) ||
        (e->type.storage < PIN_BOOL || e->type.storage > PIN_STR)) {
        return "requires a scalar compile-time value";
    }
    /* The integer walk spends the evaluation's own budget. */
    if (e->kind != SCRIPT_EXPR_CONSTANT && constant_at(analysis, index, depth, steps)) {
        script_fold_error failure;
        return script_integer_fold(analysis, index, out, &failure)
                 ? NULL
                 : fold_message(failure);
    }
    switch (e->kind) {
        case SCRIPT_EXPR_CONSTANT: *out = e->constant; return NULL;
        case SCRIPT_EXPR_SYMBOL:
            return evaluate(storage, analysis, analysis->symbols[e->symbol].initializer, out, depth + 1, steps);
        case SCRIPT_EXPR_UNARY:
        case SCRIPT_EXPR_BINARY:
        case SCRIPT_EXPR_CAST: break;
        default: return "requires scalar compile-time values and operators";
    }
    pin_value left = { 0 }, right = { 0 };
    const char *error = evaluate(storage, analysis, e->left, &left, depth + 1, steps);
    if (error) {
        return error;
    }
    if (e->kind == SCRIPT_EXPR_CAST) {
        if (!convert(storage, &left, e->type.storage, e->conversion)) {
            return behavior_scalar_cast_supported(left.type, e->type.storage)
                     ? "constant does not fit its conversion"
                     : "unsupported scalar constant conversion";
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
            return evaluate(storage, analysis, e->right, out, depth + 1, steps);
        }
        error = evaluate(storage, analysis, e->right, &right, depth + 1, steps);
        if (error) {
            return error;
        }
        script_type x = analysis->expressions[e->left].type,
                    y = analysis->expressions[e->right].type;
        if (script_type_numeric(x) && script_type_numeric(y)) {
            pin_type operand = script_analyzer_arithmetic_type(x, y).storage;
            if (!convert(storage, &left, operand, BEHAVIOR_CAST_EXACT) ||
                !convert(storage, &right, operand, BEHAVIOR_CAST_EXACT)) {
                return "unsupported scalar constant conversion";
            }
        } else if (
            left.type != right.type && !(behavior_scalar_string(left.type) &&
                                         behavior_scalar_string(right.type))
        ) {
            return "incompatible scalar constant operands";
        }
    }
    return script_scalar_operation(e->op, left, right, e->type.storage, out);
}
const char *script_scalar_evaluate(
    arena *storage,
    const script_analysis *analysis,
    int expression,
    pin_type expected,
    pin_value *out
)
{
    int steps = SCRIPT_SCALAR_CONSTANT_STEPS;
    pin_value result = { 0 };
    const char *error = evaluate(storage, analysis, expression, &result, 0, &steps);
    if (error) {
        return error;
    }
    if (!convert(storage, &result, expected, BEHAVIOR_CAST_EXACT)) {
        return "incompatible scalar constant result";
    }
    *out = result;
    return NULL;
}
