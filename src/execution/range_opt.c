#include "execution/definition.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>

/* Facts describe values already computed, never mutable component storage or
 * a presentation range. Unknown calls, joins and resumptions end the proof.
 * Floating intervals include infinities; nan records the separate unordered
 * case. MIN/MAX use Chibi's ordered comparison and right-operand tie rule. */
typedef struct
{
    double low, high;
    bool nan;
    int epoch, integer, definition;
} interval;

static interval unknown(pin_type type)
{
    interval r = { .low = -INFINITY, .high = INFINITY, .nan = true, .integer = -1 };
    switch (type) {
        case PIN_BOOL:
            r.low = 0;
            r.high = 1;
            break;
        case PIN_I8:
            r.low = INT8_MIN;
            r.high = INT8_MAX;
            break;
        case PIN_U8:
            r.low = 0;
            r.high = UINT8_MAX;
            break;
        case PIN_I16:
            r.low = INT16_MIN;
            r.high = INT16_MAX;
            break;
        case PIN_U16:
            r.low = 0;
            r.high = UINT16_MAX;
            break;
        case PIN_I32:
            r.low = INT32_MIN;
            r.high = INT32_MAX;
            break;
        case PIN_U32:
            r.low = 0;
            r.high = UINT32_MAX;
            break;
        default: return r;
    }
    r.nan = false;
    return r;
}
static interval literal(pin_value value)
{
    interval r = unknown(value.type);
    double number;
    switch (value.type) {
        case PIN_BOOL: number = value.b; break;
        case PIN_I8: number = value.i8; break;
        case PIN_U8: number = value.u8; break;
        case PIN_I16: number = value.i16; break;
        case PIN_U16: number = value.u16; break;
        case PIN_I32: number = value.i; break;
        case PIN_U32: number = value.u; break;
        case PIN_F32: number = value.f; break;
        case PIN_F64: number = value.d; break;
        default: return r;
    }
    if (!isnan(number)) {
        r.low = r.high = number;
        r.nan = false;
    }
    return r;
}
static interval value(const behavior_function *f, const interval *facts, int epoch, int slot)
{
    return facts[slot].epoch == epoch ? facts[slot] : unknown(f->slots[slot]);
}
static void copy(behavior_function_instruction *i, int slot)
{
    i->op = BF_CAST;
    i->left = slot;
    i->right = i->target = 0;
    i->left_slot = true;
    i->right_slot = false;
}
static void boolean(behavior_function_instruction *i, bool result)
{
    i->op = BF_CONSTANT;
    i->type = PIN_BOOL;
    i->value = (pin_value){ .type = PIN_BOOL, .b = result };
    i->left = i->right = i->target = 0;
    i->left_slot = i->right_slot = false;
}
static int comparison(behavior_function_op op, interval a, interval b)
{
    if (a.nan || b.nan) {
        return -1;
    }
    switch (op) {
        case BF_LT: return a.high < b.low ? 1 : a.low >= b.high ? 0 : -1;
        case BF_LE: return a.high <= b.low ? 1 : a.low > b.high ? 0 : -1;
        case BF_GT: return comparison(BF_LT, b, a);
        case BF_GE: return comparison(BF_LE, b, a);
        case BF_EQ:
        case BF_NE:
            if (a.high < b.low || b.high < a.low) {
                return op == BF_NE;
            }
            if (a.low == a.high && b.low == b.high && a.low == b.low) {
                return op == BF_EQ;
            }
            return -1;
        default: return -1;
    }
}
static interval arithmetic(behavior_function_op op, pin_type type, interval a, interval b)
{
    interval r = unknown(type);
    if (a.nan || b.nan || !isfinite(a.low) || !isfinite(a.high) ||
        !isfinite(b.low) || !isfinite(b.high)) {
        return r;
    }
    if (op == BF_ADD) {
        r.low = a.low + b.low;
        r.high = a.high + b.high;
    } else if (op == BF_SUB) {
        r.low = a.low - b.high;
        r.high = a.high - b.low;
    } else if (op == BF_MUL) {
        double products[] = { a.low * b.low, a.low * b.high, a.high * b.low, a.high * b.high };
        r.low = r.high = products[0];
        for (int j = 1; j < 4; j++) {
            r.low = fmin(r.low, products[j]);
            r.high = fmax(r.high, products[j]);
        }
    } else {
        return r;
    }
    /* Outward rounding admits every represented intermediate. It deliberately
     * forgoes exact endpoints instead of reassociating IEEE arithmetic. */
    if (type == PIN_F32) {
        r.low = nextafterf((float)r.low, -INFINITY);
        r.high = nextafterf((float)r.high, INFINITY);
    } else {
        r.low = nextafter(r.low, -INFINITY);
        r.high = nextafter(r.high, INFINITY);
    }
    r.nan = false;
    return r;
}
static bool barrier(behavior_function_op op)
{
    return op == BF_BRANCH || op == BF_JUMP || op == BF_ENTER || op == BF_LEAVE ||
           op == BF_SUSPEND || op == BF_DELAY || op == BF_POINT ||
           op == BF_SOURCE_POINT || op == BF_HOST_CALL || op == BF_INVOKE ||
           op == BF_QUERY || op == BF_COMMAND || op == BF_COMMAND_BEGIN;
}
void behavior_function_optimize_ranges(behavior_function *f, const bool *keep)
{
    int count = acount(f->instructions), slots = acount(f->slots);
    bool candidate = false;
    for (int pc = 0; pc < count; pc++) {
        behavior_function_op op = f->runtime_body[pc].op;
        candidate |= keep[pc] && (op == BF_MIN || op == BF_MAX ||
                                  op == BF_ABS || (op >= BF_LT && op <= BF_NE));
    }
    if (!candidate) {
        return;
    }
    interval *facts = xcalloc((size_t)(slots + 1), sizeof *facts);
    int *definitions = xcalloc((size_t)(slots + 1), sizeof *definitions);
    bool *entry = xcalloc((size_t)(count + 1), sizeof *entry);
    for (int pc = 0; pc < count; pc++) {
        const behavior_function_instruction *i = &f->runtime_body[pc];
        if (i->op == BF_BRANCH || i->op == BF_JUMP) {
            entry[i->target] = true;
        }
    }
    int epoch = 1;
    for (int pc = 0; pc < count; pc++) {
        behavior_function_instruction *i = &f->runtime_body[pc];
        if (entry[pc]) {
            epoch++;
        }
        if (barrier(i->op)) {
            epoch++;
        }
        if (!keep[pc] || i->op >= BF_STORE) {
            continue;
        }
        interval a = i->left_slot ? value(f, facts, epoch, i->left) : unknown(i->type);
        interval b = i->right_slot ? value(f, facts, epoch, i->right)
                                   : unknown(i->type);
        interval r = unknown(f->slots[i->output]);
        bool real = i->type == PIN_F32 || i->type == PIN_F64;
        if (i->op == BF_CONSTANT) {
            r = literal(i->value);
        } else if (i->op == BF_CAST && f->slots[i->left] == i->type) {
            r = a;
        } else if (i->op == BF_CAST && i->type == PIN_F64 && f->slots[i->left] >= PIN_BOOL && f->slots[i->left] <= PIN_U32) {
            r = a;
            r.integer = i->left;
            r.definition = definitions[i->left];
        } else if (i->op == BF_CONVERT) {
            r = a;
            r.integer = -1;
            if (i->type == PIN_F32) {
                r.low = (float)r.low;
                r.high = (float)r.high;
            }
        } else if (real && i->op == BF_ABS) {
            r.low = a.low <= 0 && a.high >= 0 ? 0 : fmin(fabs(a.low), fabs(a.high));
            r.high = fmax(fabs(a.low), fabs(a.high));
            r.nan = a.nan;
            if (!a.nan && a.low > 0) {
                copy(i, i->left);
            }
        } else if (real && (i->op == BF_ADD || i->op == BF_SUB || i->op == BF_MUL)) {
            r = arithmetic(i->op, i->type, a, b);
        } else if (real && (i->op == BF_MIN || i->op == BF_MAX)) {
            bool minimum = i->op == BF_MIN;
            bool left = !a.nan && !b.nan &&
                        (minimum
                             ? a.high < b.low || (a.high == b.low && a.high != 0)
                             : a.low > b.high || (a.low == b.high && a.low != 0));
            bool right = minimum ? a.low >= b.high : a.high <= b.low;
            if (left || right) {
                r = left ? a : b;
                copy(i, left ? i->left : i->right);
            } else {
                r.low = minimum ? fmin(a.low, b.low) : fmax(a.low, b.low);
                r.high = minimum ? fmin(a.high, b.high) : fmax(a.high, b.high);
                if (a.nan) {
                    r.low = fmin(r.low, b.low);
                    r.high = fmax(r.high, b.high);
                }
                r.nan = b.nan;
            }
        } else if (i->op >= BF_LT && i->op <= BF_NE) {
            int result = comparison(i->op, a, b);
            if (result >= 0) {
                boolean(i, result);
                r = literal(i->value);
            } else if (
                f->slots[i->left] == PIN_F64 && a.integer >= 0 &&
                b.integer >= 0 && definitions[a.integer] == a.definition &&
                definitions[b.integer] == b.definition &&
                f->slots[a.integer] == f->slots[b.integer] &&
                (f->slots[a.integer] == PIN_I32 ||
                 (f->slots[a.integer] == PIN_U32 && (i->op == BF_EQ || i->op == BF_NE)))
            ) {
                /* Widening <=32-bit integers to F64 is exact and injective.
                 * Comparing the originals removes both conversions without
                 * narrowing any source-language number or its arithmetic. */
                i->left = a.integer;
                i->right = b.integer;
                i->type = f->slots[a.integer];
            }
        }
        r.epoch = epoch;
        facts[i->output] = r;
        definitions[i->output]++;
        if ((i->op == BF_DIV || i->op == BF_MOD) &&
            (i->type == PIN_I32 || i->type == PIN_I64) && i->target >= 0) {
            facts[i->target] = unknown(i->type);
            definitions[i->target]++;
        }
    }
    free(entry);
    free(definitions);
    free(facts);
}
