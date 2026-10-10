#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Units of measure, as F# checks them, erased before execution. Reflected
 * fields and globals carry a unit such as "u/s"; arithmetic combines them and
 * a mismatch is a warning. A plain number literal adapts to the unit it meets;
 * a value nothing measures is unknown and never warns. */
#define TERMS 8

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
    apush(p->a->out->warnings, d);
}
static void warn_units(pass *p, script_range range, unit x, const char *op, unit y)
{
    char left[64], right[64];
    format(x, left, sizeof left);
    format(y, right, sizeof right);
    warn(p, range, "Units differ: %s %s %s", left, op, right);
}
/* `+`, `-`, `%` and comparisons: the operands share one unit. */
static unit match(pass *p, script_range range, unit x, const char *op, unit y)
{
    if (clash(x, y)) {
        warn_units(p, range, x, op, y);
        return x;
    }
    if (x.kind == UNIT_KNOWN || y.kind == UNIT_KNOWN) {
        return x.kind == UNIT_KNOWN ? x : y;
    }
    return x.kind == UNIT_LITERAL && y.kind == UNIT_LITERAL ? LITERAL : UNKNOWN;
}
static unit product(unit x, unit y, int sign)
{
    if (x.kind == UNIT_UNKNOWN || y.kind == UNIT_UNKNOWN) {
        return UNKNOWN;
    }
    if (y.kind == UNIT_LITERAL) {
        return x;
    }
    unit out = x.kind == UNIT_LITERAL ? (unit){ .kind = UNIT_KNOWN } : x;
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
static script_operator operator_named(const char *name, int arity)
{
    for (script_operator op = SCRIPT_OP_ADD; op <= SCRIPT_OP_GE; op++) {
        const char *text = symbol_text(op);
        if (text && !strcmp(text, name)) {
            return arity == 1 && op == SCRIPT_OP_SUB ? SCRIPT_OP_NEG : op;
        }
    }
    return SCRIPT_OP_NONE;
}
static unit expression_unit(pass *p, int index);

static unit operation(pass *p, script_range range, script_operator op, int left, int right)
{
    unit x = expression_unit(p, left);
    unit y = right >= 0 ? expression_unit(p, right) : UNKNOWN;
    switch (op) {
        case SCRIPT_OP_NEG: return x;
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
static unit call(pass *p, const script_expression *e)
{
    const script_catalog_capability
        *capability = script_catalog_capability_by_id(p->a->catalog, e->capability);
    if (!capability || !capability->operator_name || e->argument_count < 1 ||
        e->argument_count > 2) {
        return UNKNOWN;
    }
    script_operator op = operator_named(capability->operator_name, e->argument_count);
    int right = e->argument_count == 2 ? e->arguments[1].value : -1;
    return operation(p, e->range, op, e->arguments[0].value, right);
}
static unit member(pass *p, const script_expression *e)
{
    const script_expression *receiver = &p->a->out->expressions[e->left];
    const script_catalog_type
        *type = script_catalog_type_by_id(p->a->catalog, receiver->type.nominal);
    for (int i = 0; type && i < type->field_count; i++) {
        if (type->fields[i].id == e->member && type->fields[i].unit) {
            return parse(type->fields[i].unit);
        }
    }
    /* A field of a measured struct, such as a velocity's x, shares its unit. */
    unit whole = expression_unit(p, e->left);
    return whole.kind == UNIT_KNOWN ? whole : UNKNOWN;
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
            return e->symbol >= 0 ? symbol(p, e->symbol) : UNKNOWN;
        case SCRIPT_EXPR_UNARY:
        case SCRIPT_EXPR_BINARY:
            return operation(p, e->range, e->op, e->left, e->right);
        case SCRIPT_EXPR_CAST: return expression_unit(p, e->left);
        case SCRIPT_EXPR_CALL: return call(p, e);
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
static void assignment(pass *p, const script_statement *s)
{
    unit target = expression_unit(p, s->expression);
    unit value = expression_unit(p, s->second);
    switch (s->op) {
        case SCRIPT_OP_NONE:
            if (clash(target, value)) {
                warn_units(p, s->range, target, "=", value);
            }
            return;
        case SCRIPT_OP_ADD:
        case SCRIPT_OP_SUB:
        case SCRIPT_OP_MOD:
            if (clash(target, value)) {
                warn_units(
                    p,
                    s->range,
                    target,
                    s->op == SCRIPT_OP_ADD   ? "+="
                    : s->op == SCRIPT_OP_SUB ? "-="
                                             : "%=",
                    value
                );
            }
            return;
        case SCRIPT_OP_MUL:
        case SCRIPT_OP_DIV:
            if (target.kind == UNIT_KNOWN && value.kind == UNIT_KNOWN && value.count) {
                char before[64], after[64];
                format(target, before, sizeof before);
                format(product(target, value, s->op == SCRIPT_OP_MUL ? 1 : -1), after, sizeof after);
                warn(
                    p,
                    s->range,
                    "Units differ: %s would turn %s into %s",
                    s->op == SCRIPT_OP_MUL ? "*=" : "/=",
                    before,
                    after
                );
            }
            return;
        default: return;
    }
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
    free(p.units);
    free(p.visits);
}
