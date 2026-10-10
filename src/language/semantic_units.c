#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Units of measure, as F# checks them, erased before execution. Reflected
 * fields and globals carry a unit such as "u/s"; arithmetic combines them and
 * a mismatch is a warning. A plain number literal adapts to the unit it meets;
 * a value nothing measures is unknown and never warns. */
#define TERMS 8
/* A unit as text: every term, with a power and a separator. */
#define UNIT_TEXT 192

typedef enum
{
    UNIT_UNKNOWN,
    UNIT_LITERAL,
    UNIT_KNOWN,
} unit_kind;
typedef struct
{
    const char *name;
    int length, power;
} term;
typedef struct
{
    unit_kind kind;
    int count;
    term terms[TERMS]; /* In written order; no zero powers. */
} unit;
typedef enum
{
    PENDING,
    VISITING,
    DONE,
} visit;
typedef struct
{
    analyzer *a;
    unit *units;
    visit *visits;
} pass;

static const unit UNKNOWN = { .kind = UNIT_UNKNOWN };
static const unit LITERAL = { .kind = UNIT_LITERAL };
static const char SQUARED[] = "²", CUBED[] = "³", DOT[] = "·";

static bool same_name(const term *x, const term *y)
{
    return x->length == y->length && !memcmp(x->name, y->name, (size_t)x->length);
}
/* The term of `u` named like `t`, or u->count. */
static int find(const unit *u, const term *t)
{
    int i = 0;
    while (i < u->count && !same_name(&u->terms[i], t)) {
        i++;
    }
    return i;
}
/* Multiplies `out` by `t`. False when the result needs too many terms. */
static bool multiply_term(unit *out, term t)
{
    int i = find(out, &t);
    if (i < out->count) {
        out->terms[i].power += t.power;
        if (!out->terms[i].power) {
            memmove(&out->terms[i], &out->terms[i + 1], (size_t)(out->count - i - 1) * sizeof t);
            out->count--;
        }
        return true;
    }
    if (out->count == TERMS) {
        return false;
    }
    out->terms[out->count++] = t;
    return true;
}
static bool starts(const char *text, const char *prefix)
{
    return !strncmp(text, prefix, strlen(prefix));
}
/* Display text such as "u/s²", "m/s^2" or "kg·m/s²"; anything else is unknown. */
static unit parse(const char *text)
{
    if (!text || !*text) {
        return UNKNOWN;
    }
    unit out = { .kind = UNIT_KNOWN };
    int sign = 1;
    for (const char *c = text; *c;) {
        term t = { .name = c, .power = 1 };
        while (*c && *c != '/' && *c != '^' && !starts(c, SQUARED) &&
               !starts(c, CUBED) && !starts(c, DOT)) {
            c++;
        }
        t.length = (int)(c - t.name);
        if (*c == '^') {
            t.power = (int)strtol(c + 1, (char **)&c, 10);
        } else if (starts(c, SQUARED) || starts(c, CUBED)) {
            t.power = starts(c, SQUARED) ? 2 : 3;
            c += strlen(SQUARED);
        }
        bool one = t.length == 1 && *t.name == '1';
        t.power *= sign;
        if (!t.length || !t.power || (!one && !multiply_term(&out, t))) {
            return UNKNOWN;
        }
        if (starts(c, DOT)) {
            c += strlen(DOT);
        } else if (*c == '/') {
            sign = -1;
            c++;
        } else if (*c) {
            return UNKNOWN;
        }
    }
    return out;
}
static void append_term(char *out, size_t size, term t)
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
/* Cuts a sequence a full buffer split, so a long unit stays valid UTF-8. */
static void whole_characters(char *text)
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
static void format(unit u, char *out, size_t size)
{
    *out = 0;
    if (u.kind != UNIT_KNOWN) {
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
    whole_characters(out);
}
static bool same(unit x, unit y)
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
static bool clash(unit x, unit y)
{
    return x.kind == UNIT_KNOWN && y.kind == UNIT_KNOWN && !same(x, y);
}
static void warn(pass *p, script_range range, const char *format_text, ...)
{
    script_diagnostic d = { .range = range };
    va_list args;
    va_start(args, format_text);
    vsnprintf(d.message, sizeof d.message, format_text, args);
    va_end(args);
    whole_characters(d.message);
    apush(p->a->out->warnings, d);
}
static void warn_units(pass *p, script_range range, unit x, const char *op, unit y)
{
    char left[UNIT_TEXT], right[UNIT_TEXT];
    format(x, left, sizeof left);
    format(y, right, sizeof right);
    warn(p, range, "Units differ: %s %s %s", left, op, right);
}
/* `+`, `-`, `%` and comparisons: the operands share one unit. A mismatch warns
 * once, here, and leaves the result unknown. */
static unit match(pass *p, script_range range, unit x, const char *op, unit y)
{
    if (clash(x, y)) {
        warn_units(p, range, x, op, y);
        return UNKNOWN;
    }
    if (x.kind == UNIT_KNOWN || y.kind == UNIT_KNOWN) {
        return x.kind == UNIT_KNOWN ? x : y;
    }
    return x.kind == UNIT_LITERAL && y.kind == UNIT_LITERAL ? LITERAL : UNKNOWN;
}
/* `*` and `/`. A literal factor may carry any unit, as `3` in `3 * dt` may
 * be 3 rad/s, so its product is unknown. */
static unit product(unit x, unit y, int sign)
{
    if (x.kind == UNIT_LITERAL && y.kind == UNIT_LITERAL) {
        return LITERAL;
    }
    if (x.kind != UNIT_KNOWN || y.kind != UNIT_KNOWN) {
        return UNKNOWN;
    }
    unit out = x;
    for (int i = 0; i < y.count; i++) {
        term t = y.terms[i];
        t.power *= sign;
        if (!multiply_term(&out, t)) {
            return UNKNOWN;
        }
    }
    return out;
}
static const char *symbol_text(script_operator op)
{
    switch (op) {
        case SCRIPT_OP_ADD: return "+";
        case SCRIPT_OP_SUB: return "-";
        case SCRIPT_OP_MUL: return "*";
        case SCRIPT_OP_DIV: return "/";
        case SCRIPT_OP_MOD: return "%";
        case SCRIPT_OP_EQ: return "==";
        case SCRIPT_OP_NE: return "!=";
        case SCRIPT_OP_LT: return "<";
        case SCRIPT_OP_LE: return "<=";
        case SCRIPT_OP_GT: return ">";
        case SCRIPT_OP_GE: return ">=";
        default: return NULL;
    }
}
static unit expression_unit(pass *p, int index);

static unit operation(pass *p, script_range range, script_operator op, int left, int right)
{
    unit x = expression_unit(p, left);
    unit y = right >= 0 ? expression_unit(p, right) : UNKNOWN;
    if (right < 0) {
        /* Unary `-x` and `+x` keep x's unit; `!` and `~` measure nothing. */
        return op == SCRIPT_OP_NEG || op == SCRIPT_OP_NONE ? x : UNKNOWN;
    }
    switch (op) {
        case SCRIPT_OP_MUL: return product(x, y, 1);
        case SCRIPT_OP_DIV: return product(x, y, -1);
        case SCRIPT_OP_ADD:
        case SCRIPT_OP_SUB:
        case SCRIPT_OP_MOD: return match(p, range, x, symbol_text(op), y);
        case SCRIPT_OP_EQ:
        case SCRIPT_OP_NE:
        case SCRIPT_OP_LT:
        case SCRIPT_OP_LE:
        case SCRIPT_OP_GT:
        case SCRIPT_OP_GE:
            match(p, range, x, symbol_text(op), y);
            return UNKNOWN;
        default: return UNKNOWN;
    }
}
static unit member(pass *p, const script_expression *e)
{
    const script_expression *receiver = &p->a->out->expressions[e->left];
    const script_catalog_type
        *type = script_catalog_type_by_id(p->a->catalog, receiver->type.nominal);
    for (int i = 0; type && i < type->field_count; i++) {
        if (type->fields[i].id == e->member) {
            return parse(type->fields[i].unit);
        }
    }
    return UNKNOWN;
}
static unit symbol(pass *p, int index)
{
    const script_symbol *s = &p->a->out->symbols[index];
    if (s->kind == SCRIPT_GLOBAL) {
        for (int i = 0; i < p->a->catalog->global_count; i++) {
            if (p->a->catalog->globals[i].id == s->id) {
                return parse(p->a->catalog->globals[i].unit);
            }
        }
        return UNKNOWN;
    }
    if (s->kind != SCRIPT_LOCAL || s->initializer < 0) {
        return UNKNOWN;
    }
    /* A local measures what its initializer does; `n := 0` stays unknown. */
    unit initial = expression_unit(p, s->initializer);
    return initial.kind == UNIT_KNOWN ? initial : UNKNOWN;
}
static unit compute(pass *p, const script_expression *e)
{
    switch (e->kind) {
        case SCRIPT_EXPR_CONSTANT:
            return script_type_numeric(e->type) ? LITERAL : UNKNOWN;
        case SCRIPT_EXPR_SYMBOL:
            assert(e->symbol >= 0);
            return symbol(p, e->symbol);
        case SCRIPT_EXPR_UNARY:
        case SCRIPT_EXPR_BINARY:
            return operation(p, e->range, e->op, e->left, e->right);
        case SCRIPT_EXPR_CAST: return expression_unit(p, e->left);
        case SCRIPT_EXPR_MEMBER: return member(p, e);
        default: return UNKNOWN;
    }
}
static unit expression_unit(pass *p, int index)
{
    if (index < 0 || p->visits[index] == VISITING) {
        return UNKNOWN;
    }
    if (p->visits[index] == PENDING) {
        p->visits[index] = VISITING;
        p->units[index] = compute(p, &p->a->out->expressions[index]);
        p->visits[index] = DONE;
    }
    return p->units[index];
}
/* `*=` and `/=` keep the target's unit only with an unmeasured factor. */
static void scale(pass *p, const script_statement *s, unit target, unit value)
{
    if (target.kind != UNIT_KNOWN || value.kind != UNIT_KNOWN || !value.count) {
        return;
    }
    char before[UNIT_TEXT], after[UNIT_TEXT];
    format(target, before, sizeof before);
    format(product(target, value, s->op == SCRIPT_OP_MUL ? 1 : -1), after, sizeof after);
    warn(p, s->range, "Units differ: %s= would turn %s into %s", symbol_text(s->op), before, after);
}
static void assignment(pass *p, const script_statement *s)
{
    unit target = expression_unit(p, s->expression);
    unit value = expression_unit(p, s->second);
    switch (s->op) {
        case SCRIPT_OP_NONE:
        case SCRIPT_OP_ADD:
        case SCRIPT_OP_SUB:
        case SCRIPT_OP_MOD:
            if (clash(target, value)) {
                char op[4];
                snprintf(op, sizeof op, "%s=", s->op == SCRIPT_OP_NONE ? "" : symbol_text(s->op));
                warn_units(p, s->range, target, op, value);
            }
            return;
        case SCRIPT_OP_MUL:
        case SCRIPT_OP_DIV: scale(p, s, target, value); return;
        default: return;
    }
}
static int by_position(const void *a, const void *b)
{
    const script_diagnostic *x = a, *y = b;
    return (x->range.start > y->range.start) - (x->range.start < y->range.start);
}
void script_analyzer_units(analyzer *a)
{
    script_analysis *out = a->out;
    int count = acount(out->expressions);
    pass p = { a,
               xcalloc((size_t)count + 1, sizeof *p.units),
               xcalloc((size_t)count + 1, sizeof *p.visits) };
    for (int i = 0; i < count; i++) {
        expression_unit(&p, i);
    }
    for (int i = 0; i < acount(out->statements); i++) {
        if (out->statements[i].kind == SCRIPT_STMT_ASSIGN) {
            assignment(&p, &out->statements[i]);
        }
    }
    for (int i = 0; i < acount(out->bindings); i++) {
        const script_binding *b = &out->bindings[i];
        unit target = expression_unit(&p, b->target);
        unit value = expression_unit(&p, b->value);
        if (clash(target, value)) {
            warn_units(&p, b->range, target, "=", value);
        }
    }
    if (acount(out->warnings) > 1) {
        qsort(out->warnings, (size_t)acount(out->warnings), sizeof *out->warnings, by_position);
    }
    free(p.units);
    free(p.visits);
}
