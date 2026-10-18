#include "language/component.h"
#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reflected fields and globals carry erased, nominal units such as "u/s".
 * Known incompatible dimensions are errors. Unmeasured values remain unknown;
 * they provide no dimensional proof. */
#define TERMS 8
/* A unit as text: every term, with a power and a separator. */
#define UNIT_TEXT 192

typedef enum
{
    UNIT_UNKNOWN,
    UNIT_LITERAL,
    UNIT_KNOWN,
    UNIT_INVALID,
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
static const unit INVALID = { .kind = UNIT_INVALID };
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
static unit parse(const char *text)
{
    if (!text) {
        return UNKNOWN;
    }
    if (!*text) {
        return INVALID;
    }
    unit out = { .kind = UNIT_KNOWN };
    int sign = 1;
    for (const char *c = text; *c;) {
        term t = { .name = c, .power = 1 };
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
/* Unit nodes contain flat terms and separators; omit trivia without changing
 * dimension spelling. The same metadata parser also checks native signatures. */
static char *signature_unit(analyzer *a, TSNode node)
{
    char *text = NULL;
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_is_extra(child)) {
            continue;
        }
        char *part = script_analyzer_text_copy(a, child);
        if (script_analyzer_is(child, "identifier") &&
            (!(script_builtin_type(part).flags & SCRIPT_TYPE_ERROR) ||
             script_analyzer_find_type(a, node, part) >= 0)) {
            script_analyzer_diagnostic(a, child, "'%s' is a type, not a unit", part);
        }
        for (const char *c = part; *c; c++) {
            apush(text, *c);
        }
        free(part);
    }
    apush(text, 0);
    char *owned = xstrdup(text);
    afree(text);
    if (parse(owned).kind == UNIT_INVALID) {
        script_analyzer_diagnostic(a, node, "Invalid unit '%s'", owned);
    }
    return owned;
}
script_type script_analyzer_signature_type(analyzer *a, TSNode node, char **unit_text)
{
    *unit_text = NULL;
    TSNode measured = script_analyzer_field(node, "unit");
    if (script_analyzer_is(node, "unit")) {
        *unit_text = signature_unit(a, node);
        return script_builtin_type("number");
    }
    if (script_analyzer_is(node, "measured_type")) {
        script_type type = script_analyzer_type_node(a, script_analyzer_field(node, "base"));
        *unit_text = signature_unit(a, measured);
        if (!script_type_numeric(type)) {
            script_analyzer_diagnostic(a, node, "A measured signature requires a numeric storage type");
        }
        return type;
    }
    if (script_analyzer_is(node, "type")) {
        TSNode name_node = script_analyzer_field(node, "name");
        char *name = script_analyzer_name_copy(a, name_node);
        int symbol = script_analyzer_lookup_name(a, name);
        bool named_type = symbol >= 0 &&
                          (a->out->symbols[symbol].kind == SCRIPT_TYPE ||
                           a->out->symbols[symbol].kind == SCRIPT_TYPE_PARAMETER);
        if (script_unit_shorthand(name) && !named_type &&
            script_analyzer_find_type(a, node, name) == -1 &&
            ts_node_child_count(node) == 1) {
            *unit_text = name;
            return script_builtin_type("number");
        }
        free(name);
    }
    return script_analyzer_type_node(a, node);
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
void script_analyzer_signature_unit(analyzer *a, TSNode node, const char *expected, const char *actual)
{
    unit x = parse(expected), y = parse(actual);
    if (x.kind == UNIT_KNOWN && y.kind == UNIT_KNOWN && !same(x, y)) {
        char left[UNIT_TEXT], right[UNIT_TEXT];
        format(x, left, sizeof left);
        format(y, right, sizeof right);
        script_analyzer_diagnostic(a, node, "Units differ: %s annotation %s", left, right);
    }
}
static bool clash(unit x, unit y)
{
    return x.kind == UNIT_KNOWN && y.kind == UNIT_KNOWN && !same(x, y);
}
static void report(pass *p, script_range range, const char *format_text, ...)
{
    script_diagnostic d = { .range = range };
    va_list args;
    va_start(args, format_text);
    vsnprintf(d.message, sizeof d.message, format_text, args);
    va_end(args);
    whole_characters(d.message);
    apush(p->a->out->diagnostics, d);
}
static unit annotated(pass *p, script_range range, const char *text)
{
    unit value = parse(text);
    if (value.kind == UNIT_INVALID) {
        report(p, range, "Invalid unit '%s'", text);
        return UNKNOWN;
    }
    return value;
}
static void report_units(pass *p, script_range range, unit x, const char *op, unit y)
{
    char left[UNIT_TEXT], right[UNIT_TEXT];
    format(x, left, sizeof left);
    format(y, right, sizeof right);
    report(p, range, "Units differ: %s %s %s", left, op, right);
}
/* `+`, `-`, `%` and comparisons: the operands share one unit. A mismatch
 * reports once, here, and leaves the result unknown. */
static unit match(pass *p, script_range range, unit x, const char *op, unit y)
{
    if (clash(x, y)) {
        report_units(p, range, x, op, y);
        return UNKNOWN;
    }
    if (x.kind == UNIT_KNOWN || y.kind == UNIT_KNOWN) {
        return x.kind == UNIT_KNOWN ? x : y;
    }
    return x.kind == UNIT_LITERAL && y.kind == UNIT_LITERAL ? LITERAL : UNKNOWN;
}
/* `*` and `/`. A literal factor may carry any unit, as `3` in `3 * dt` may
 * be 3 rad/s, so its product is unknown. */
static unit product(pass *p, script_range range, unit x, unit y, int sign)
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
            report(p, range, "Unit expression exceeds supported dimensional complexity");
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
static const script_catalog_capability *call_capability(pass *p, const script_expression *e)
{
    uint64_t id = e->capability;
    if (e->kind == SCRIPT_EXPR_JOIN && e->left >= 0) {
        id = p->a->out->expressions[e->left].type.nominal;
    }
    return script_catalog_capability_by_id(p->a->catalog, id);
}
static unit result_unit(pass *p, const script_expression *e, int index)
{
    if (!e) {
        return UNKNOWN;
    }
    if ((e->kind == SCRIPT_EXPR_CALL || e->kind == SCRIPT_EXPR_JOIN) && e->right >= 0) {
        const script_function *f = &p->a->out->functions[e->right];
        return index < f->result_count
                 ? annotated(p, e->range, p->a->out->symbols[f->results[index]].unit)
                 : UNKNOWN;
    }
    const script_catalog_capability *c = call_capability(p, e);
    return c && index < c->result_count
             ? annotated(p, e->range, c->results[index].unit)
             : UNKNOWN;
}
static unit call(pass *p, const script_expression *e)
{
    const script_function *f = e->right >= 0 ? &p->a->out->functions[e->right] : NULL;
    const script_catalog_capability *c = f ? NULL : call_capability(p, e);
    for (int i = 0; i < e->argument_count; i++) {
        const script_argument *argument = &e->arguments[i];
        int index = argument->parameter;
        const char *text = NULL;
        if (index >= 0 && f && index < f->parameter_count) {
            text = p->a->out->symbols[f->parameters[index]].unit;
        } else if (index >= 0 && c && index < c->parameter_count) {
            text = c->parameters[index].unit;
        } else if (c && c->event_sender && index >= c->parameter_count) {
            const script_catalog_capability
                *event = script_catalog_capability_by_id(p->a->catalog, e->member);
            index -= c->parameter_count;
            if (event && index < event->parameter_count) {
                text = event->parameters[index].unit;
            }
        } else if (c && c->variadic && c->parameter_count && index >= c->parameter_count) {
            text = c->parameters[c->parameter_count - 1].unit;
        }
        match(
            p,
            argument->range,
            annotated(p, argument->range, text),
            "argument",
            expression_unit(p, argument->value)
        );
    }
    return !e->task && e->result_count == 1 ? result_unit(p, e, 0) : UNKNOWN;
}

static unit operation(pass *p, script_range range, script_operator op, int left, int right)
{
    unit x = expression_unit(p, left);
    unit y = right >= 0 ? expression_unit(p, right) : UNKNOWN;
    if (right < 0) {
        /* Unary `-x` and `+x` keep x's unit; `!` and `~` measure nothing. */
        return op == SCRIPT_OP_NEG || op == SCRIPT_OP_NONE ? x : UNKNOWN;
    }
    switch (op) {
        case SCRIPT_OP_MUL: return product(p, range, x, y, 1);
        case SCRIPT_OP_DIV: return product(p, range, x, y, -1);
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
    /* Authored parameter/result ports retain their own symbol identity. */
    for (int i = 0; i < p->a->out->symbol_count; i++) {
        const script_symbol *s = &p->a->out->symbols[i];
        if (s->id == e->member &&
            (s->kind == SCRIPT_PARAMETER || s->kind == SCRIPT_RESULT)) {
            return annotated(p, e->range, s->unit);
        }
    }
    const script_expression *receiver = &p->a->out->expressions[e->left];
    const script_expression *source = script_analyzer_result_source(p->a, e->left);
    const script_catalog_capability *c = source ? call_capability(p, source) : NULL;
    if (source &&
        (source->kind == SCRIPT_EXPR_CALL || source->kind == SCRIPT_EXPR_JOIN) &&
        c && e->offset < (size_t)c->result_count &&
        e->member == script_catalog_id("port", c->results[e->offset].name)) {
        return result_unit(p, source, (int)e->offset);
    }
    const script_catalog_capability
        *payload = script_catalog_capability_by_id(p->a->catalog, receiver->type.nominal);
    if ((receiver->type.flags & SCRIPT_TYPE_RESULTS) && payload &&
        payload->kind == SCRIPT_CAPABILITY_EVENT &&
        e->offset < (size_t)payload->parameter_count) {
        return annotated(p, e->range, payload->parameters[e->offset].unit);
    }
    const script_catalog_type
        *type = script_catalog_type_by_id(p->a->catalog, receiver->type.nominal);
    for (int i = 0; type && i < type->field_count; i++) {
        if (type->fields[i].id == e->member) {
            return annotated(p, e->range, type->fields[i].unit);
        }
    }
    return UNKNOWN;
}
static unit symbol(pass *p, int index, script_range range)
{
    const script_symbol *s = &p->a->out->symbols[index];
    if (s->unit) {
        return annotated(p, range, s->unit);
    }
    if (s->kind == SCRIPT_GLOBAL) {
        for (int i = 0; i < p->a->catalog->global_count; i++) {
            if (p->a->catalog->globals[i].id == s->id) {
                return annotated(p, range, p->a->catalog->globals[i].unit);
            }
        }
        return UNKNOWN;
    }
    if (s->kind == SCRIPT_LOCAL && s->initializer < 0) {
        for (int i = 0; i < p->a->out->expression_count; i++) {
            const script_expression *condition = &p->a->out->expressions[i];
            if (condition->kind == SCRIPT_EXPR_IS && condition->symbol == index) {
                return expression_unit(p, condition->left);
            }
        }
    }
    if ((s->kind != SCRIPT_LOCAL && s->kind != SCRIPT_CONSTANT &&
         s->kind != SCRIPT_STATE && s->kind != SCRIPT_SHARED_STATE) ||
        s->initializer < 0) {
        return UNKNOWN;
    }
    /* Storage measures what its initializer does; `n := 0` stays unknown. */
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
            return symbol(p, e->symbol, e->range);
        case SCRIPT_EXPR_UNARY:
        case SCRIPT_EXPR_BINARY:
            return operation(p, e->range, e->op, e->left, e->right);
        case SCRIPT_EXPR_CAST: return expression_unit(p, e->left);
        case SCRIPT_EXPR_UNWRAP:
        case SCRIPT_EXPR_AS: return expression_unit(p, e->left);
        case SCRIPT_EXPR_CALL: return call(p, e);
        case SCRIPT_EXPR_JOIN:
            return e->result_count == 1 ? result_unit(p, e, 0) : UNKNOWN;
        case SCRIPT_EXPR_FALLBACK: {
            unit result = expression_unit(p, e->left);
            for (int i = 0; i < e->argument_count; i++) {
                int value = e->arguments[i].value;
                if (value != e->left) {
                    result = match(p, e->arguments[i].range, result, "fallback", expression_unit(p, value));
                }
            }
            return result;
        }
        case SCRIPT_EXPR_STORE:
            return match(p, e->range, expression_unit(p, e->left), "=", expression_unit(p, e->right));
        case SCRIPT_EXPR_STRUCT: {
            if (!e->argument_count) {
                return UNKNOWN;
            }
            const script_catalog_type
                *type = script_catalog_type_by_id(p->a->catalog, e->type.nominal);
            assert(type);
            for (int i = 0; i < e->argument_count; i++) {
                const script_argument *argument = &e->arguments[i];
                assert(argument->parameter >= 0 && argument->parameter < type->field_count);
                unit target = annotated(
                    p,
                    argument->range,
                    type->fields[argument->parameter].unit
                );
                match(
                    p,
                    argument->range,
                    target,
                    "=",
                    expression_unit(p, argument->value)
                );
            }
            return UNKNOWN;
        }
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
    format(product(p, s->range, target, value, s->op == SCRIPT_OP_MUL ? 1 : -1), after, sizeof after);
    report(p, s->range, "Units differ: %s= would turn %s into %s", symbol_text(s->op), before, after);
}
static void assignment(pass *p, const script_statement *s)
{
    unit target = s->expression >= 0 ? expression_unit(p, s->expression)
                : s->symbol >= 0     ? symbol(p, s->symbol, s->range)
                                     : UNKNOWN;
    unit value = expression_unit(p, s->second);
    switch (s->op) {
        case SCRIPT_OP_NONE:
        case SCRIPT_OP_ADD:
        case SCRIPT_OP_SUB:
        case SCRIPT_OP_MOD:
            if (clash(target, value)) {
                char op[4];
                snprintf(op, sizeof op, "%s=", s->op == SCRIPT_OP_NONE ? "" : symbol_text(s->op));
                report_units(p, s->range, target, op, value);
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
    if (x->range.document != y->range.document) {
        return (x->range.document > y->range.document) -
               (x->range.document < y->range.document);
    }
    return (x->range.start > y->range.start) - (x->range.start < y->range.start);
}
void script_analyzer_units(analyzer *a)
{
    script_analysis *out = a->out;
    int count = acount(out->expressions);
    pass p = { a,
               xcalloc((size_t)count + 1, sizeof *p.units),
               xcalloc((size_t)count + 1, sizeof *p.visits) };
    for (int i = 0; i < out->symbol_count; i++) {
        const script_symbol *s = &out->symbols[i];
        if (s->unit) {
            unit expected = annotated(&p, s->declaration, s->unit);
            if (s->initializer >= 0) {
                match(&p, s->declaration, expected, "default", expression_unit(&p, s->initializer));
            }
        }
    }
    for (int i = 0; i < count; i++) {
        expression_unit(&p, i);
    }
    for (int i = 0; i < acount(out->statements); i++) {
        if (out->statements[i].kind == SCRIPT_STMT_ASSIGN ||
            out->statements[i].kind == SCRIPT_STMT_REDUCE) {
            assignment(&p, &out->statements[i]);
        }
    }
    for (int i = 0; i < acount(out->bindings); i++) {
        const script_binding *b = &out->bindings[i];
        unit target = expression_unit(&p, b->target);
        unit value = expression_unit(&p, b->value);
        if (clash(target, value)) {
            report_units(&p, b->range, target, "=", value);
        }
    }
    if (acount(out->diagnostics) > 1) {
        qsort(out->diagnostics, (size_t)acount(out->diagnostics), sizeof *out->diagnostics, by_position);
    }
    free(p.units);
    free(p.visits);
}
