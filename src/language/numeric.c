#include "language/numeric.h"

#include "support/arena.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Source-only private-core draft. No semantic/default/cast-policy wiring. */
#define LIMBS (SCRIPT_NUMERIC_MAX_BITS / 32)
#define SCRATCH 16

typedef struct
{
    unsigned count;
    uint32_t words[LIMBS];
} magnitude;
struct script_number
{
    const script_numeric *owner;
    unsigned short numerator_count, denominator_count;
    unsigned char kind;
    bool negative;
    uint32_t limbs[];
};
struct script_numeric
{
    arena values;
    script_numeric_limits left;
    script_numeric_error fatal;
    unsigned published;
    magnitude scratch[SCRATCH]; /* Entire scratch peak is reserved at new(). */
};

_Static_assert(
    CHAR_BIT == 8 && sizeof(float) == 4 && sizeof(double) == 8,
    "numeric constants require IEEE binary32/binary64 storage"
);
_Static_assert(
    FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 && FLT_MIN_EXP == -125 &&
        DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024 && DBL_MIN_EXP == -1021,
    "numeric constants require IEEE binary32/binary64 precision"
);

static bool fail(script_numeric *n, script_numeric_error *e, script_numeric_code code, uint64_t needed, uint64_t limit)
{
    *e = (script_numeric_error){ code, needed, limit };
    if (code >= SCRIPT_NUMERIC_BITS) {
        n->fatal = *e;
    }
    return false;
}
static bool charge(script_numeric *n, uint64_t units, script_numeric_error *e)
{
    if (n->fatal.code) {
        *e = n->fatal;
        return false;
    }
    if (units > n->left.work) {
        return fail(n, e, SCRIPT_NUMERIC_WORK, units, n->left.work);
    }
    n->left.work -= units;
    return true;
}
script_numeric *script_numeric_new(const script_numeric_limits *limits, script_numeric_error *e)
{
    assert(e);
    *e = (script_numeric_error){ 0 };
    script_numeric_limits budget = limits ? *limits
                                          : (script_numeric_limits){
                                                SCRIPT_NUMERIC_MAX_STEPS,
                                                SCRIPT_NUMERIC_MAX_WORK,
                                                SCRIPT_NUMERIC_MAX_STORAGE
                                            };
    assert(
        budget.steps <= SCRIPT_NUMERIC_MAX_STEPS &&
        budget.work <= SCRIPT_NUMERIC_MAX_WORK && budget.storage <= SCRIPT_NUMERIC_MAX_STORAGE
    );
    if (budget.storage < sizeof(script_numeric)) {
        *e = (script_numeric_error){ SCRIPT_NUMERIC_STORAGE,
                                     sizeof(script_numeric),
                                     budget.storage };
        return NULL;
    }
    uint64_t clear_work = (sizeof(script_numeric) + 3) / 4;
    if (budget.work < clear_work) {
        *e = (script_numeric_error){ SCRIPT_NUMERIC_WORK, clear_work, budget.work };
        return NULL;
    }
    budget.work -= clear_work;
    script_numeric *n = xcalloc(1, sizeof *n);
    n->left = budget;
    return n;
}
void script_numeric_free(script_numeric *n)
{
    if (!n) {
        return;
    }
    arena_free(&n->values);
    free(n);
}
script_numeric_usage script_numeric_usage_read(const script_numeric *n)
{
    assert(n);
    return (script_numeric_usage){ n->left.steps,
                                   n->left.work,
                                   sizeof *n + arena_storage_bytes(&n->values),
                                   n->published };
}
bool script_numeric_visit(script_numeric *n, unsigned depth, script_numeric_error *e)
{
    assert(n && e);
    *e = (script_numeric_error){ 0 };
    if (n->fatal.code) {
        *e = n->fatal;
        return false;
    }
    if (depth >= SCRIPT_NUMERIC_MAX_DEPTH) {
        return fail(n, e, SCRIPT_NUMERIC_DEPTH, depth, SCRIPT_NUMERIC_MAX_DEPTH);
    }
    if (!n->left.steps) {
        return fail(n, e, SCRIPT_NUMERIC_STEPS, 1, 0);
    }
    n->left.steps--;
    /* Covers fixed-size metadata, exponent/sign branches and bit-length work. */
    return charge(n, 64, e);
}
static unsigned bits(const magnitude *a)
{
    if (!a->count) {
        return 0;
    }
    uint32_t top = a->words[a->count - 1];
    unsigned tail = 0;
    if (top >> 16) {
        top >>= 16;
        tail += 16;
    }
    if (top >> 8) {
        top >>= 8;
        tail += 8;
    }
    if (top >> 4) {
        top >>= 4;
        tail += 4;
    }
    if (top >> 2) {
        top >>= 2;
        tail += 2;
    }
    if (top >> 1) {
        tail++;
    }
    return (a->count - 1) * 32 + tail + 1;
}
static void trim(magnitude *a)
{
    while (a->count && !a->words[a->count - 1]) {
        a->count--;
    }
}
static bool one(const magnitude *a)
{
    return a->count == 1 && a->words[0] == 1;
}
static bool set(script_numeric *n, magnitude *a, uint32_t value, script_numeric_error *e)
{
    if (!charge(n, 1, e)) {
        return false;
    }
    a->count = value != 0;
    a->words[0] = value;
    return true;
}
static bool copy(script_numeric *n, magnitude *out, const magnitude *a, script_numeric_error *e)
{
    if (!charge(n, 1 + a->count, e)) {
        return false;
    }
    if (out != a) {
        memcpy(out->words, a->words, a->count * sizeof *a->words);
        out->count = a->count;
    }
    return true;
}
static int compare(script_numeric *n, const magnitude *a, const magnitude *b, script_numeric_error *e)
{
    unsigned count = a->count > b->count ? a->count : b->count;
    if (!charge(n, 32 + count, e)) {
        return 0;
    }
    if (a->count != b->count) {
        return a->count > b->count ? 1 : -1;
    }
    for (unsigned i = count; i; i--) {
        if (a->words[i - 1] != b->words[i - 1]) {
            return a->words[i - 1] > b->words[i - 1] ? 1 : -1;
        }
    }
    return 0;
}
/* These three helpers permit out == a and/or out == b. Capacity is fixed;
 * carry is rejected before any write that would expand past 4,096 bits. */
static bool add(script_numeric *n, magnitude *out, const magnitude *a, const magnitude *b, script_numeric_error *e)
{
    unsigned an = a->count, bn = b->count, count = an > bn ? an : bn;
    if (!charge(n, 32 + 4 * (uint64_t)count, e)) {
        return false;
    }
    uint64_t carry = 0;
    for (unsigned i = 0; i < count; i++) {
        uint64_t value = carry + (i < an ? a->words[i] : 0) +
                         (uint64_t)(i < bn ? b->words[i] : 0);
        out->words[i] = (uint32_t)value;
        carry = value >> 32;
    }
    out->count = count;
    if (carry) {
        if (count == LIMBS) {
            return fail(n, e, SCRIPT_NUMERIC_BITS, SCRIPT_NUMERIC_MAX_BITS + 1, SCRIPT_NUMERIC_MAX_BITS);
        }
        out->words[out->count++] = (uint32_t)carry;
    }
    return true;
}
static bool subtract(
    script_numeric *n,
    magnitude *out,
    const magnitude *a,
    const magnitude *b,
    script_numeric_error *e
)
{
    unsigned an = a->count, bn = b->count;
    if (!charge(n, 32 + 5 * (uint64_t)an, e)) {
        return false;
    }
    uint64_t borrow = 0;
    for (unsigned i = 0; i < an; i++) {
        uint64_t x = a->words[i], y = (i < bn ? b->words[i] : 0) + borrow;
        out->words[i] = (uint32_t)(x - y);
        borrow = x < y;
    }
    assert(!borrow && an >= bn);
    out->count = an;
    trim(out);
    return true;
}
static bool small_add(script_numeric *n, magnitude *a, uint32_t value, script_numeric_error *e)
{
    if (!charge(n, 32 + 4 * (uint64_t)a->count, e)) {
        return false;
    }
    uint64_t carry = value;
    for (unsigned i = 0; i < a->count && carry; i++) {
        uint64_t x = (uint64_t)a->words[i] + carry;
        a->words[i] = (uint32_t)x;
        carry = x >> 32;
    }
    if (carry) {
        if (a->count == LIMBS) {
            return fail(n, e, SCRIPT_NUMERIC_BITS, SCRIPT_NUMERIC_MAX_BITS + 1, SCRIPT_NUMERIC_MAX_BITS);
        }
        a->words[a->count++] = (uint32_t)carry;
    }
    return true;
}
static bool small_multiply(script_numeric *n, magnitude *a, uint32_t value, script_numeric_error *e)
{
    if (!charge(n, 32 + 4 * (uint64_t)a->count, e)) {
        return false;
    }
    uint64_t carry = 0;
    for (unsigned i = 0; i < a->count; i++) {
        uint64_t x = (uint64_t)a->words[i] * value + carry;
        a->words[i] = (uint32_t)x;
        carry = x >> 32;
    }
    if (carry) {
        if (a->count == LIMBS) {
            return fail(n, e, SCRIPT_NUMERIC_BITS, SCRIPT_NUMERIC_MAX_BITS + 1, SCRIPT_NUMERIC_MAX_BITS);
        }
        a->words[a->count++] = (uint32_t)carry;
    }
    trim(a);
    return true;
}
static bool multiply(
    script_numeric *n,
    magnitude *out,
    const magnitude *a,
    const magnitude *b,
    script_numeric_error *e
)
{
    assert(out != a && out != b);
    if (!a->count || !b->count) {
        return set(n, out, 0, e);
    }
    unsigned lower = bits(a) + bits(b) - 1;
    if (lower > SCRIPT_NUMERIC_MAX_BITS) {
        return fail(n, e, SCRIPT_NUMERIC_BITS, lower, SCRIPT_NUMERIC_MAX_BITS);
    }
    unsigned count = a->count + b->count;
    if (count > LIMBS) {
        count = LIMBS;
    }
    if (!charge(n, 32 + count + 8 * (uint64_t)a->count * b->count, e)) {
        return false;
    }
    memset(out->words, 0, count * sizeof *out->words);
    out->count = count;
    for (unsigned i = 0; i < a->count; i++) {
        uint64_t carry = 0;
        for (unsigned j = 0; j < b->count; j++) {
            assert(i + j < LIMBS);
            uint64_t value = (uint64_t)a->words[i] * b->words[j] +
                             out->words[i + j] + carry;
            out->words[i + j] = (uint32_t)value;
            carry = value >> 32;
        }
        if (carry) {
            if (i + b->count == LIMBS) {
                return fail(n, e, SCRIPT_NUMERIC_BITS, SCRIPT_NUMERIC_MAX_BITS + 1, SCRIPT_NUMERIC_MAX_BITS);
            }
            out->words[i + b->count] = (uint32_t)carry;
        }
    }
    trim(out);
    return true;
}
static bool shift_left(script_numeric *n, magnitude *out, const magnitude *a, unsigned shift, script_numeric_error *e)
{
    unsigned width = bits(a), old = a->count;
    if (width && shift > SCRIPT_NUMERIC_MAX_BITS - width) {
        return fail(n, e, SCRIPT_NUMERIC_BITS, (uint64_t)width + shift, SCRIPT_NUMERIC_MAX_BITS);
    }
    unsigned count = width ? (width + shift + 31) / 32 : 0;
    if (!charge(n, 32 + 6 * (uint64_t)count, e)) {
        return false;
    }
    unsigned words = shift / 32, tail = shift % 32;
    for (unsigned i = count; i; i--) {
        unsigned at = i - 1;
        uint32_t value = 0;
        if (at >= words) {
            unsigned source = at - words;
            value = source < old ? a->words[source] << tail : 0;
            if (tail && source && source - 1 < old) {
                value |= a->words[source - 1] >> (32 - tail);
            }
        }
        out->words[at] = value;
    }
    out->count = count;
    return true;
}
static bool shift_right(
    script_numeric *n,
    magnitude *out,
    const magnitude *a,
    unsigned shift,
    bool *discarded,
    script_numeric_error *e
)
{
    unsigned width = bits(a), old = a->count;
    if (!charge(n, 32 + 8 * (uint64_t)old, e)) {
        return false;
    }
    *discarded = false;
    if (shift >= width) {
        *discarded = old != 0;
        out->count = 0;
        return true;
    }
    unsigned words = shift / 32, tail = shift % 32;
    for (unsigned i = 0; i < words; i++) {
        *discarded |= a->words[i] != 0;
    }
    if (tail) {
        *discarded |= (a->words[words] & (UINT32_MAX >> (32 - tail))) != 0;
    }
    unsigned count = (width - shift + 31) / 32;
    for (unsigned i = 0; i < count; i++) {
        unsigned source = i + words;
        uint32_t value = a->words[source] >> tail;
        if (tail && source + 1 < old) {
            value |= a->words[source + 1] << (32 - tail);
        }
        out->words[i] = value;
    }
    out->count = count;
    trim(out);
    return true;
}
/* r < d. Compute (2*r + bit) % d without creating a 4,097-bit temporary. */
static bool advance(
    script_numeric *n,
    magnitude *r,
    const magnitude *d,
    unsigned bit,
    magnitude *delta,
    unsigned *quotient_bit,
    script_numeric_error *e
)
{
    if (!subtract(n, delta, d, r, e)) {
        return false;
    }
    int order = compare(n, r, delta, e);
    if (e->code) {
        return false;
    }
    *quotient_bit = order >= 0;
    if (order >= 0) {
        if (!subtract(n, r, r, delta, e)) {
            return false;
        }
    } else if (!add(n, r, r, r, e)) {
        return false;
    }
    if (bit && !small_add(n, r, bit, e)) {
        return false;
    }
    if (!*quotient_bit) {
        order = compare(n, r, d, e);
        if (e->code) {
            return false;
        }
        if (order >= 0) {
            *quotient_bit = 1;
            return subtract(n, r, r, d, e);
        }
    }
    return true;
}
static bool divide(
    script_numeric *n,
    const magnitude *a,
    const magnitude *b,
    magnitude *q,
    magnitude *r,
    magnitude *delta,
    script_numeric_error *e
)
{
    assert(b->count && q != a && q != b && r != a && r != b && q != r);
    if (!charge(n, 32 + a->count, e)) {
        return false;
    }
    q->count = a->count;
    memset(q->words, 0, q->count * sizeof *q->words);
    r->count = 0;
    unsigned width = bits(a);
    for (unsigned i = width; i; i--) {
        unsigned at = i - 1, next;
        if (!advance(n, r, b, (a->words[at / 32] >> (at % 32)) & 1, delta, &next, e)) {
            return false;
        }
        q->words[at / 32] |= (uint32_t)next << (at % 32);
    }
    trim(q);
    return true;
}
/* Scratch 10..14 are reserved to GCD. Inputs/output must be outside them. */
static bool gcd(script_numeric *n, const magnitude *a, const magnitude *b, magnitude *out, script_numeric_error *e)
{
    magnitude *u = &n->scratch[10], *v = &n->scratch[11], *q = &n->scratch[12],
              *r = &n->scratch[13], *delta = &n->scratch[14];
    if (!copy(n, u, a, e) || !copy(n, v, b, e)) {
        return false;
    }
    while (v->count) {
        if (one(v)) {
            return set(n, out, 1, e);
        }
        if (!divide(n, u, v, q, r, delta, e)) {
            return false;
        }
        magnitude *old = u;
        u = v;
        v = r;
        r = old;
    }
    return copy(n, out, u, e);
}
static bool load(script_numeric *n, const script_number *value, magnitude *a, magnitude *b, script_numeric_error *e)
{
    assert(value && value->owner == n);
    unsigned an = value->numerator_count, bn = value->denominator_count;
    if (!charge(n, 32 + an + bn, e)) {
        return false;
    }
    a->count = an;
    memcpy(a->words, value->limbs, an * sizeof *a->words);
    b->count = bn ? bn : 1;
    if (bn) {
        memcpy(b->words, value->limbs + an, bn * sizeof *b->words);
    } else {
        b->words[0] = 1;
    }
    return true;
}
/* Cancel shared factors in-place, using scratch 6..14, before expansion. */
static bool cancel(script_numeric *n, magnitude *a, magnitude *b, script_numeric_error *e)
{
    magnitude *g = &n->scratch[6], *q = &n->scratch[7], *r = &n->scratch[8],
              *delta = &n->scratch[9];
    if (!gcd(n, a, b, g, e)) {
        return false;
    }
    if (!g->count || one(g)) {
        return true;
    }
    if (!divide(n, a, g, q, r, delta, e) || !copy(n, a, q, e)) {
        return false;
    }
    assert(!r->count);
    if (!divide(n, b, g, q, r, delta, e) || !copy(n, b, q, e)) {
        return false;
    }
    assert(!r->count);
    return true;
}
static const script_number *publish(
    script_numeric *n,
    magnitude *a,
    magnitude *b,
    script_number_kind kind,
    bool negative,
    script_numeric_error *e
)
{
    if (kind == SCRIPT_NUMBER_REAL && !a->count && !set(n, b, 1, e)) {
        return NULL;
    }
    if (kind == SCRIPT_NUMBER_REAL && !one(b)) {
        assert(b->count);
        /* Caller numerator/denominator are outside scratch 6..14. */
        if (!cancel(n, a, b, e)) {
            return NULL;
        }
    }
    unsigned an = a->count, bn = kind == SCRIPT_NUMBER_REAL ? b->count : 0;
    if (!charge(n, 32 + an + bn, e)) {
        return NULL;
    }
    size_t bytes = sizeof(script_number) + (size_t)(an + bn) * sizeof(uint32_t);
    size_t budget = n->left.storage - sizeof *n;
    script_number *out = arena_alloc_bounded(&n->values, bytes, budget);
    if (!out) {
        /* Growth may need a complete retained block, not just bytes.
         * Report the configured total cap without inventing a growth estimate. */
        fail(n, e, SCRIPT_NUMERIC_STORAGE, 0, n->left.storage);
        return NULL;
    }
    *out = (script_number){ .owner = n,
                            .numerator_count = (unsigned short)an,
                            .denominator_count = (unsigned short)bn,
                            .kind = (unsigned char)kind,
                            .negative = negative && (an || kind == SCRIPT_NUMBER_REAL) };
    memcpy(out->limbs, a->words, an * sizeof *out->limbs);
    if (bn) {
        memcpy(out->limbs + an, b->words, bn * sizeof *out->limbs);
    }
    n->published++;
    return out;
}
script_number_kind script_number_kind_read(const script_number *value)
{
    assert(value);
    return (script_number_kind)value->kind;
}
static int digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}
const script_number *script_number_read(
    script_numeric *n,
    const char *text,
    size_t length,
    script_number_kind kind,
    script_numeric_error *e
)
{
    assert(text && (kind == SCRIPT_NUMBER_INTEGER || kind == SCRIPT_NUMBER_REAL));
    if (!script_numeric_visit(n, 0, e) || !charge(n, length, e)) {
        return NULL;
    }
    magnitude *a = &n->scratch[0], *b = &n->scratch[1];
    if (!set(n, a, 0, e) || !set(n, b, 1, e)) {
        return NULL;
    }
    if (length >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        if (kind != SCRIPT_NUMBER_INTEGER || length == 2) {
            fail(n, e, SCRIPT_NUMERIC_LITERAL, 0, 0);
            return NULL;
        }
        for (size_t i = 2; i < length; i++) {
            int value = digit(text[i]);
            if (value < 0) {
                fail(n, e, SCRIPT_NUMERIC_LITERAL, i, length);
                return NULL;
            }
            if (!small_multiply(n, a, 16, e) || !small_add(n, a, (uint32_t)value, e)) {
                return NULL;
            }
        }
        return publish(n, a, b, kind, false, e);
    }
    size_t at = 0, fractional = 0, last_nonzero = 0, trailing = 0;
    bool dot = false, exponent = false, after_dot = false, nonzero = false;
    while (at < length) {
        char c = text[at];
        if (c >= '0' && c <= '9') {
            if (dot) {
                fractional++;
                after_dot = true;
            }
            if (c != '0') {
                last_nonzero = at;
                trailing = 0;
                nonzero = true;
            }
            if (c == '0') {
                trailing++;
            }
            at++;
            continue;
        }
        if (c == '.' && at && !dot) {
            dot = true;
            at++;
            continue;
        }
        break;
    }
    size_t mantissa_end = at;
    int decimal_exponent = 0;
    if (at < length && (text[at] == 'e' || text[at] == 'E')) {
        exponent = true;
        at++;
        bool negative = at < length && text[at] == '-';
        if (at < length && (text[at] == '+' || text[at] == '-')) {
            at++;
        }
        size_t first = at;
        for (; at < length && text[at] >= '0' && text[at] <= '9'; at++) {
            int next = text[at] - '0';
            if (decimal_exponent > (SCRIPT_NUMERIC_MAX_BITS - next) / 10) {
                fail(n, e, SCRIPT_NUMERIC_BITS, SCRIPT_NUMERIC_MAX_BITS + 1, SCRIPT_NUMERIC_MAX_BITS);
                return NULL;
            }
            decimal_exponent = decimal_exponent * 10 + next;
        }
        if (at == first) {
            fail(n, e, SCRIPT_NUMERIC_LITERAL, at, length);
            return NULL;
        }
        if (negative) {
            decimal_exponent = -decimal_exponent;
        }
    }
    if (!mantissa_end || text[0] < '0' || text[0] > '9' || (dot && !after_dot) ||
        at != length || (kind == SCRIPT_NUMBER_REAL) != (dot || exponent)) {
        fail(n, e, SCRIPT_NUMERIC_LITERAL, at, length);
        return NULL;
    }
    if (!nonzero) {
        return publish(n, a, b, kind, false, e);
    }
    /* length was charged against <= 2^24 work: these conversions are bounded. */
    int scale = decimal_exponent - (int)fractional + (int)trailing;
    if (scale < -1233 || scale > 1233) {
        fail(n, e, SCRIPT_NUMERIC_BITS, SCRIPT_NUMERIC_MAX_BITS + 1, SCRIPT_NUMERIC_MAX_BITS);
        return NULL;
    }
    for (size_t i = 0; i <= last_nonzero; i++) {
        if (text[i] == '.') {
            continue;
        }
        if (!small_multiply(n, a, 10, e) ||
            !small_add(n, a, (uint32_t)(text[i] - '0'), e)) {
            return NULL;
        }
    }
    magnitude *scaled = scale < 0 ? b : a;
    unsigned count = (unsigned)(scale < 0 ? -scale : scale);
    for (unsigned i = 0; i < count; i++) {
        if (!small_multiply(n, scaled, 10, e)) {
            return NULL;
        }
    }
    return publish(n, a, b, kind, false, e);
}
const script_number *script_number_unary(
    script_numeric *n,
    script_operator op,
    const script_number *value,
    script_numeric_error *e
)
{
    if (!script_numeric_visit(n, 0, e)) {
        return NULL;
    }
    magnitude *a = &n->scratch[0], *b = &n->scratch[1], *unit = &n->scratch[2];
    if (!load(n, value, a, b, e)) {
        return NULL;
    }
    bool negative = value->negative;
    if (op == SCRIPT_OP_NEG) {
        return publish(n, a, b, (script_number_kind)value->kind, !negative, e);
    }
    if (op != SCRIPT_OP_BIT_NOT) {
        fail(n, e, SCRIPT_NUMERIC_OPERATOR, op, 0);
        return NULL;
    }
    if (value->kind != SCRIPT_NUMBER_INTEGER) {
        fail(n, e, SCRIPT_NUMERIC_KIND, value->kind, SCRIPT_NUMBER_INTEGER);
        return NULL;
    }
    if (negative) {
        if (!set(n, unit, 1, e) || !subtract(n, a, a, unit, e)) {
            return NULL;
        }
    } else if (!small_add(n, a, 1, e)) {
        return NULL;
    }
    return publish(n, a, b, SCRIPT_NUMBER_INTEGER, !negative, e);
}
static bool twos(
    script_numeric *n,
    magnitude *out,
    const magnitude *a,
    unsigned width,
    bool negative,
    script_numeric_error *e
)
{
    unsigned count = (width + 31) / 32, old = a->count;
    if (!charge(n, 32 + 6 * (uint64_t)count, e)) {
        return false;
    }
    uint64_t carry = negative;
    for (unsigned i = 0; i < count; i++) {
        uint32_t word = i < old ? a->words[i] : 0;
        uint64_t value = negative ? (uint64_t)(uint32_t)~word + carry : word;
        out->words[i] = (uint32_t)value;
        carry = value >> 32;
    }
    if (width % 32) {
        out->words[count - 1] &= UINT32_MAX >> (32 - width % 32);
    }
    out->count = count;
    trim(out);
    return true;
}
static const script_number *bitwise(
    script_numeric *n,
    script_operator op,
    const script_number *left,
    const script_number *right,
    script_numeric_error *e
)
{
    magnitude *a = &n->scratch[0], *b = &n->scratch[2], *out = &n->scratch[4],
              *den = &n->scratch[5];
    if (left->kind != SCRIPT_NUMBER_INTEGER || right->kind != SCRIPT_NUMBER_INTEGER) {
        fail(n, e, SCRIPT_NUMERIC_KIND, SCRIPT_NUMBER_REAL, SCRIPT_NUMBER_INTEGER);
        return NULL;
    }
    bool negative = left->negative;
    if (op == SCRIPT_OP_SHIFT_LEFT || op == SCRIPT_OP_SHIFT_RIGHT) {
        if (right->negative) {
            fail(n, e, SCRIPT_NUMERIC_NEGATIVE_SHIFT, 0, 0);
            return NULL;
        }
        if (b->count > 1 || (b->count && b->words[0] > SCRIPT_NUMERIC_MAX_SHIFT)) {
            fail(n, e, SCRIPT_NUMERIC_SHIFT_LIMIT, SCRIPT_NUMERIC_MAX_SHIFT + 1, SCRIPT_NUMERIC_MAX_SHIFT);
            return NULL;
        }
        unsigned shift = b->count ? b->words[0] : 0;
        if (op == SCRIPT_OP_SHIFT_LEFT) {
            if (!shift_left(n, out, a, shift, e)) {
                return NULL;
            }
        } else {
            bool discarded;
            if (!shift_right(n, out, a, shift, &discarded, e)) {
                return NULL;
            }
            if (negative && discarded && !small_add(n, out, 1, e)) {
                return NULL;
            }
        }
        return publish(n, out, den, SCRIPT_NUMBER_INTEGER, negative, e);
    }
    unsigned ab = bits(a), bb = bits(b), width = (ab > bb ? ab : bb) + 1;
    if (width > SCRIPT_NUMERIC_MAX_BITS) {
        fail(n, e, SCRIPT_NUMERIC_BITS, width, SCRIPT_NUMERIC_MAX_BITS);
        return NULL;
    }
    if (!twos(n, a, a, width, left->negative, e) ||
        !twos(n, b, b, width, right->negative, e)) {
        return NULL;
    }
    unsigned count = (width + 31) / 32;
    if (!charge(n, 32 + 5 * (uint64_t)count, e)) {
        return NULL;
    }
    for (unsigned i = 0; i < count; i++) {
        uint32_t x = i < a->count ? a->words[i] : 0,
                 y = i < b->count ? b->words[i] : 0;
        switch (op) {
            case SCRIPT_OP_BIT_AND: out->words[i] = x & y; break;
            case SCRIPT_OP_BIT_OR: out->words[i] = x | y; break;
            case SCRIPT_OP_BIT_XOR: out->words[i] = x ^ y; break;
            default: assert(false); break;
        }
    }
    out->count = count;
    negative = (out->words[(width - 1) / 32] >> ((width - 1) % 32)) & 1;
    if (!twos(n, out, out, width, negative, e)) {
        return NULL;
    }
    return publish(n, out, den, SCRIPT_NUMBER_INTEGER, negative, e);
}
const script_number *script_number_binary(
    script_numeric *n,
    script_operator op,
    const script_number *left,
    const script_number *right,
    script_numeric_error *e
)
{
    if (!script_numeric_visit(n, 0, e)) {
        return NULL;
    }
    magnitude *a = &n->scratch[0], *ad = &n->scratch[1], *b = &n->scratch[2],
              *bd = &n->scratch[3], *out = &n->scratch[4], *den = &n->scratch[5];
    if (!load(n, left, a, ad, e) || !load(n, right, b, bd, e)) {
        return NULL;
    }
    if (op >= SCRIPT_OP_BIT_AND && op <= SCRIPT_OP_SHIFT_RIGHT) {
        return bitwise(n, op, left, right, e);
    }
    script_number_kind kind = left->kind == SCRIPT_NUMBER_REAL || right->kind == SCRIPT_NUMBER_REAL
                                ? SCRIPT_NUMBER_REAL
                                : SCRIPT_NUMBER_INTEGER;
    bool negative = left->negative ^ right->negative;
    if ((op == SCRIPT_OP_DIV || op == SCRIPT_OP_MOD) && !b->count) {
        fail(n, e, SCRIPT_NUMERIC_ZERO_DIVISOR, 0, 0);
        return NULL;
    }
    if ((op == SCRIPT_OP_DIV || op == SCRIPT_OP_MOD) && kind == SCRIPT_NUMBER_INTEGER) {
        if (!divide(n, a, b, out, den, &n->scratch[6], e)) {
            return NULL;
        }
        if (op == SCRIPT_OP_MOD) {
            return publish(n, den, ad, kind, left->negative, e);
        }
        return publish(n, out, ad, kind, negative, e);
    }
    if (op == SCRIPT_OP_MOD || (op != SCRIPT_OP_ADD && op != SCRIPT_OP_SUB &&
                                op != SCRIPT_OP_MUL && op != SCRIPT_OP_DIV)) {
        fail(n, e, SCRIPT_NUMERIC_OPERATOR, op, 0);
        return NULL;
    }
    if (op == SCRIPT_OP_MUL || op == SCRIPT_OP_DIV) {
        if (op == SCRIPT_OP_DIV) {
            if (!cancel(n, a, b, e) || !cancel(n, bd, ad, e) ||
                !multiply(n, out, a, bd, e) || !multiply(n, den, ad, b, e)) {
                return NULL;
            }
        } else if (
            !cancel(n, a, bd, e) || !cancel(n, b, ad, e) ||
            !multiply(n, out, a, b, e) || !multiply(n, den, ad, bd, e)
        ) {
            return NULL;
        }
        return publish(n, out, den, kind, negative, e);
    }
    /* Denominator GCD before cross-products avoids unnecessary expansion. */
    magnitude *g = &n->scratch[6], *x = &n->scratch[7], *y = &n->scratch[8],
              *rhs = &n->scratch[9];
    if (!gcd(n, ad, bd, g, e) ||
        !divide(n, bd, g, x, &n->scratch[10], &n->scratch[11], e) ||
        !divide(n, ad, g, y, &n->scratch[10], &n->scratch[11], e) ||
        !multiply(n, out, a, x, e) || !multiply(n, rhs, b, y, e) ||
        !multiply(n, den, ad, x, e)) {
        return NULL;
    }
    bool left_negative = left->negative;
    bool right_negative = right->negative ^ (op == SCRIPT_OP_SUB);
    if (left_negative == right_negative) {
        if (!add(n, out, out, rhs, e)) {
            return NULL;
        }
        negative = left_negative;
    } else {
        int order = compare(n, out, rhs, e);
        if (e->code) {
            return NULL;
        }
        negative = order > 0 ? left_negative
                 : order < 0 ? right_negative
                             : false;
        if (order >= 0) {
            if (!subtract(n, out, out, rhs, e)) {
                return NULL;
            }
        } else if (!subtract(n, out, rhs, out, e)) {
            return NULL;
        }
    }
    return publish(n, out, den, kind, negative, e);
}
bool script_number_compare(
    script_numeric *n,
    const script_number *left,
    const script_number *right,
    int *out,
    script_numeric_error *e
)
{
    assert(out);
    if (!script_numeric_visit(n, 0, e)) {
        return false;
    }
    magnitude *a = &n->scratch[0], *ad = &n->scratch[1], *b = &n->scratch[2],
              *bd = &n->scratch[3], *x = &n->scratch[4], *y = &n->scratch[5];
    if (!load(n, left, a, ad, e) || !load(n, right, b, bd, e)) {
        return false;
    }
    if (!a->count && !b->count) {
        *out = 0;
        return true;
    }
    if (left->negative != right->negative) {
        *out = left->negative ? -1 : 1;
        return true;
    }
    if (!cancel(n, a, b, e) || !cancel(n, ad, bd, e) ||
        !multiply(n, x, a, bd, e) || !multiply(n, y, b, ad, e)) {
        return false;
    }
    int order = compare(n, x, y, e);
    if (e->code) {
        return false;
    }
    *out = left->negative ? -order : order;
    return true;
}
static uint64_t low64(const magnitude *a)
{
    assert(a->count <= 2);
    return (a->count ? a->words[0] : 0) |
           (a->count > 1 ? (uint64_t)a->words[1] << 32 : 0);
}
static bool integer_value(
    script_numeric *n,
    const script_number *value,
    const magnitude *a,
    const magnitude *b,
    pin_type type,
    pin_value *out,
    script_numeric_error *e
)
{
    if (!one(b)) {
        return fail(n, e, SCRIPT_NUMERIC_FRACTION, 0, 0);
    }
    unsigned width;
    bool signed_integer;
    switch (type) {
        case PIN_I8:
            width = 8;
            signed_integer = true;
            break;
        case PIN_U8:
            width = 8;
            signed_integer = false;
            break;
        case PIN_I16:
            width = 16;
            signed_integer = true;
            break;
        case PIN_U16:
            width = 16;
            signed_integer = false;
            break;
        case PIN_I32:
            width = 32;
            signed_integer = true;
            break;
        case PIN_U32:
            width = 32;
            signed_integer = false;
            break;
        case PIN_I64:
            width = 64;
            signed_integer = true;
            break;
        case PIN_U64:
            width = 64;
            signed_integer = false;
            break;
        default: return fail(n, e, SCRIPT_NUMERIC_KIND, type, 0);
    }
    bool negative = value->negative && a->count;
    if (bits(a) > width || (negative && !signed_integer)) {
        return fail(n, e, SCRIPT_NUMERIC_RANGE, bits(a), width);
    }
    uint64_t magnitude_value = low64(a);
    uint64_t maximum = signed_integer ? (UINT64_C(1) << (width - 1)) - !negative
                                      : UINT64_MAX >> (64 - width);
    if (magnitude_value > maximum) {
        return fail(n, e, SCRIPT_NUMERIC_RANGE, magnitude_value, maximum);
    }
    uint64_t word = negative ? UINT64_C(0) - magnitude_value : magnitude_value;
    *out = (pin_value){ .type = type };
    switch (type) {
        case PIN_I8: {
            uint8_t v = (uint8_t)word;
            memcpy(&out->i8, &v, sizeof v);
            break;
        }
        case PIN_U8: out->u8 = (uint8_t)word; break;
        case PIN_I16: {
            uint16_t v = (uint16_t)word;
            memcpy(&out->i16, &v, sizeof v);
            break;
        }
        case PIN_U16: out->u16 = (uint16_t)word; break;
        case PIN_I32: {
            uint32_t v = (uint32_t)word;
            memcpy(&out->i, &v, sizeof v);
            break;
        }
        case PIN_U32: out->u = (uint32_t)word; break;
        case PIN_I64: memcpy(&out->i64, &word, sizeof word); break;
        case PIN_U64: out->u64 = word; break;
        default: assert(false); break;
    }
    return true;
}
static void floating_bits(pin_type type, bool negative, uint64_t payload, pin_value *out)
{
    *out = (pin_value){ .type = type };
    if (type == PIN_F32) {
        uint32_t word = (uint32_t)payload | (negative ? UINT32_C(1) << 31 : 0);
        memcpy(&out->f, &word, sizeof word);
        return;
    }
    uint64_t word = payload | (negative ? UINT64_C(1) << 63 : 0);
    memcpy(&out->d, &word, sizeof word);
}
static bool floating_value(
    script_numeric *n,
    const script_number *value,
    magnitude *a,
    magnitude *b,
    pin_type type,
    pin_value *out,
    script_numeric_error *e
)
{
    int precision = type == PIN_F32 ? 24 : 53,
        minimum = type == PIN_F32 ? -126 : -1022,
        maximum = type == PIN_F32 ? 127 : 1023, bias = type == PIN_F32 ? 127 : 1023;
    magnitude *shifted = &n->scratch[3], *q = &n->scratch[4],
              *r = &n->scratch[5], *delta = &n->scratch[6];
    if (!a->count) {
        floating_bits(type, value->negative, 0, out);
        return true;
    }
    int exponent = (int)bits(a) - (int)bits(b), order;
    if (exponent >= 0) {
        if (!shift_left(n, shifted, b, (unsigned)exponent, e)) {
            return false;
        }
        order = compare(n, a, shifted, e);
    } else {
        if (!shift_left(n, shifted, a, (unsigned)-exponent, e)) {
            return false;
        }
        order = compare(n, shifted, b, e);
    }
    if (e->code) {
        return false;
    }
    if (order < 0) {
        exponent--;
    }
    if (exponent > maximum) {
        return fail(n, e, SCRIPT_NUMERIC_RANGE, exponent, maximum);
    }
    if (exponent < minimum - precision) {
        floating_bits(type, value->negative, 0, out);
        return true;
    }
    bool normal = exponent >= minimum;
    int scale = precision - 1 - (normal ? exponent : minimum);
    const magnitude *denominator = b;
    if (scale < 0) {
        if (!shift_left(n, shifted, b, (unsigned)-scale, e)) {
            return false;
        }
        denominator = shifted;
    }
    if (!divide(n, a, denominator, q, r, delta, e)) {
        return false;
    }
    for (int i = 0; i < scale; i++) {
        unsigned next;
        if (!advance(n, r, denominator, 0, delta, &next, e) ||
            !shift_left(n, q, q, 1, e) || (next && !small_add(n, q, next, e))) {
            return false;
        }
    }
    assert(bits(q) <= (unsigned)precision);
    if (value->kind == SCRIPT_NUMBER_INTEGER && r->count) {
        return fail(n, e, SCRIPT_NUMERIC_PRECISION, bits(a), precision);
    }
    uint64_t significand = low64(q);
    if (!subtract(n, delta, denominator, r, e)) {
        return false;
    }
    order = compare(n, r, delta, e);
    if (e->code) {
        return false;
    }
    if (order > 0 || (!order && (significand & 1))) {
        significand++;
    }
    if (normal && significand == (UINT64_C(1) << precision)) {
        significand >>= 1;
        exponent++;
    }
    if (exponent > maximum) {
        return fail(n, e, SCRIPT_NUMERIC_RANGE, exponent, maximum);
    }
    uint64_t hidden = UINT64_C(1) << (precision - 1), payload;
    if (normal) {
        payload = ((uint64_t)(exponent + bias) << (precision - 1)) |
                  (significand - hidden);
    } else {
        /* significand==hidden is the exact carry to the smallest normal. */
        payload = significand;
    }
    floating_bits(type, value->negative, payload, out);
    return true;
}
bool script_number_materialize(
    script_numeric *n,
    const script_number *value,
    pin_type type,
    pin_value *out,
    script_numeric_error *e
)
{
    assert(out);
    if (!script_numeric_visit(n, 0, e)) {
        return false;
    }
    magnitude *a = &n->scratch[0], *b = &n->scratch[1];
    if (!load(n, value, a, b, e)) {
        return false;
    }
    pin_value result;
    bool ok = type == PIN_F32 || type == PIN_F64
                ? floating_value(n, value, a, b, type, &result, e)
                : integer_value(n, value, a, b, type, &result, e);
    if (ok) {
        *out = result;
    }
    return ok;
}
