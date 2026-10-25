#include "language/unit_algebra.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const script_unit UNKNOWN = { .kind = SCRIPT_UNIT_UNKNOWN };
static const script_unit LITERAL = { .kind = SCRIPT_UNIT_LITERAL };
static const script_unit INVALID = { .kind = SCRIPT_UNIT_INVALID };
static const char SQUARED[] = "²", CUBED[] = "³", DOT[] = "·";

static bool same_name(const script_unit_term *x, const script_unit_term *y)
{
    return x->length == y->length && !memcmp(x->name, y->name, (size_t)x->length);
}
/* The script_unit_term of `u` named like `t`, or u->count. */
static int find(const script_unit *u, const script_unit_term *t)
{
    int i = 0;
    while (i < u->count && !same_name(&u->terms[i], t)) {
        i++;
    }
    return i;
}
/* Multiplies `out` by `t`. False when the result needs too many terms. */
static bool multiply_term(script_unit *out, script_unit_term t)
{
    int i = find(out, &t);
    if (i < out->count) {
        int before = out->terms[i].power;
        if ((t.power > 0 && before > INT_MAX - t.power) ||
            (t.power < 0 && before < -INT_MAX - t.power)) {
            return false;
        }
        out->terms[i].power += t.power;
        if (!out->terms[i].power) {
            memmove(&out->terms[i], &out->terms[i + 1], (size_t)(out->count - i - 1) * sizeof t);
            out->count--;
        }
        return true;
    }
    if (out->count == SCRIPT_UNIT_TERMS) {
        return false;
    }
    out->terms[out->count++] = t;
    return true;
}
static bool starts(const char *text, const char *prefix)
{
    return !strncmp(text, prefix, strlen(prefix));
}
static bool letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool digit(char c)
{
    return c >= '0' && c <= '9';
}
/* Native metadata also permits integer powers and products, such as kg·m/s².
 * A missing annotation is unknown; a malformed annotation is invalid. */
script_unit script_unit_parse(const char *text)
{
    if (!text) {
        return UNKNOWN;
    }
    if (!*text) {
        return INVALID;
    }
    script_unit out = { .kind = SCRIPT_UNIT_KNOWN };
    int sign = 1;
    for (const char *c = text; *c;) {
        script_unit_term t = { .name = c, .power = 1 };
        if (*c == '1') {
            c++;
        } else {
            if (!letter(*c)) {
                return INVALID;
            }
            while (letter(*c) || digit(*c)) {
                c++;
            }
        }
        t.length = (int)(c - t.name);
        if (*c == '^') {
            const char *digits = c + 1;
            char *end;
            errno = 0;
            long power = strtol(digits, &end, 10);
            if (end == digits || errno == ERANGE || power > INT_MAX || power < -INT_MAX ||
                !power || (!digit(*digits) && *digits != '-' && *digits != '+')) {
                return INVALID;
            }
            t.power = (int)power;
            c = end;
        } else if (starts(c, SQUARED) || starts(c, CUBED)) {
            t.power = starts(c, SQUARED) ? 2 : 3;
            c += strlen(SQUARED);
        }
        bool one = t.length == 1 && *t.name == '1';
        t.power *= sign;
        if (!t.length || !t.power || (!one && !multiply_term(&out, t))) {
            return INVALID;
        }
        if (starts(c, DOT)) {
            c += strlen(DOT);
        } else if (*c == '/') {
            sign = -1;
            c++;
        } else if (*c) {
            return INVALID;
        } else {
            return out;
        }
        if (!*c) {
            return INVALID;
        }
    }
    return out;
}
static void append_term(char *out, size_t size, script_unit_term t)
{
    int power = abs(t.power);
    size_t used = strlen(out);
    snprintf(
        out + used,
        size - used,
        "%.*s%s",
        t.length,
        t.name,
        power == 1   ? ""
        : power == 2 ? SQUARED
        : power == 3 ? CUBED
                     : "^"
    );
    used = strlen(out);
    if (power > 3) {
        snprintf(out + used, size - used, "%d", power);
    }
}
/* Remove an incomplete final sequence when the output buffer cuts UTF-8. */
void script_unit_whole_characters(char *text)
{
    size_t end = strlen(text), lead = end;
    while (lead && ((unsigned char)text[lead - 1] & 0xc0) == 0x80) {
        lead--;
    }
    if (!lead) {
        return;
    }
    unsigned char first = (unsigned char)text[lead - 1];
    size_t length = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
    if (end - (lead - 1) < length) {
        text[lead - 1] = 0;
    }
}
void script_unit_format(script_unit u, char *out, size_t size)
{
    assert(out && size > 0);
    *out = 0;
    if (u.kind != SCRIPT_UNIT_KNOWN) {
        snprintf(out, size, "?");
        return;
    }
    bool numerator = false;
    for (int i = 0; i < u.count; i++) {
        if (u.terms[i].power > 0) {
            if (numerator) {
                snprintf(out + strlen(out), size - strlen(out), "%s", DOT);
            }
            append_term(out, size, u.terms[i]);
            numerator = true;
        }
    }
    if (!numerator) {
        snprintf(out, size, "1");
    }
    for (int i = 0; i < u.count; i++) {
        if (u.terms[i].power < 0) {
            snprintf(out + strlen(out), size - strlen(out), "/");
            append_term(out, size, u.terms[i]);
        }
    }
    script_unit_whole_characters(out);
}
bool script_unit_same(script_unit x, script_unit y)
{
    if (x.count != y.count) {
        return false;
    }
    for (int i = 0; i < x.count; i++) {
        int j = find(&y, &x.terms[i]);
        if (j == y.count || x.terms[i].power != y.terms[j].power) {
            return false;
        }
    }
    return true;
}
script_unit script_unit_product(script_unit x, script_unit y, int sign, bool *overflow)
{
    assert(overflow && (sign == 1 || sign == -1));
    *overflow = false;
    if (x.kind == SCRIPT_UNIT_LITERAL && y.kind == SCRIPT_UNIT_LITERAL) {
        return LITERAL;
    }
    if (x.kind != SCRIPT_UNIT_KNOWN || y.kind != SCRIPT_UNIT_KNOWN) {
        return UNKNOWN;
    }
    script_unit out = x;
    for (int i = 0; i < y.count; i++) {
        script_unit_term t = y.terms[i];
        t.power *= sign;
        if (!multiply_term(&out, t)) {
            *overflow = true;
            return UNKNOWN;
        }
    }
    return out;
}
