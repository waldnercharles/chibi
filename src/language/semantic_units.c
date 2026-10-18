#include "language/semantic_internal.h"
#include "language/unit_algebra.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reflected fields and globals carry erased, nominal units such as "u/s".
 * Known incompatible dimensions are errors. Unmeasured values remain unknown;
 * they provide no dimensional proof. */
typedef enum
{
    PENDING,
    VISITING,
    DONE,
} visit;
typedef struct
{
    analyzer *a;
    script_unit *units;
    visit *visits;
} pass;

static const script_unit UNKNOWN = { .kind = SCRIPT_UNIT_UNKNOWN };
static const script_unit LITERAL = { .kind = SCRIPT_UNIT_LITERAL };

script_type script_analyzer_signature_type(analyzer *a, TSNode node, char **unit_text)
{
    return script_analyzer_read_type(a, node, true, unit_text);
}
void script_analyzer_signature_unit(analyzer *a, TSNode node, const char *expected, const char *actual)
{
    script_unit x = script_unit_parse(expected), y = script_unit_parse(actual);
    if (x.kind == SCRIPT_UNIT_KNOWN && y.kind == SCRIPT_UNIT_KNOWN &&
        !script_unit_same(x, y)) {
        char left[SCRIPT_UNIT_TEXT], right[SCRIPT_UNIT_TEXT];
        script_unit_format(x, left, sizeof left);
        script_unit_format(y, right, sizeof right);
        script_analyzer_diagnostic(a, node, "Units differ: %s annotation %s", left, right);
    }
}
static bool clash(script_unit x, script_unit y)
{
    return x.kind == SCRIPT_UNIT_KNOWN && y.kind == SCRIPT_UNIT_KNOWN &&
           !script_unit_same(x, y);
}
static void report(pass *p, script_range range, const char *format_text, ...)
{
    script_diagnostic d = { .range = range };
    va_list args;
    va_start(args, format_text);
    vsnprintf(d.message, sizeof d.message, format_text, args);
    va_end(args);
    script_unit_whole_characters(d.message);
    apush(p->a->out->diagnostics, d);
}
static script_unit annotated(pass *p, script_range range, const char *text)
{
    script_unit value = script_unit_parse(text);
    if (value.kind == SCRIPT_UNIT_INVALID) {
        report(p, range, "Invalid unit '%s'", text);
        return UNKNOWN;
    }
    return value;
}
static void report_units(pass *p, script_range range, script_unit x, const char *op, script_unit y)
{
    char left[SCRIPT_UNIT_TEXT], right[SCRIPT_UNIT_TEXT];
    script_unit_format(x, left, sizeof left);
    script_unit_format(y, right, sizeof right);
    report(p, range, "Units differ: %s %s %s", left, op, right);
}
/* `+`, `-`, `%` and comparisons: the operands share one unit. A mismatch
 * reports once, here, and leaves the result unknown. */
static script_unit match(pass *p, script_range range, script_unit x, const char *op, script_unit y)
{
    if (clash(x, y)) {
        report_units(p, range, x, op, y);
        return UNKNOWN;
    }
    if (x.kind == SCRIPT_UNIT_KNOWN || y.kind == SCRIPT_UNIT_KNOWN) {
        return x.kind == SCRIPT_UNIT_KNOWN ? x : y;
    }
    return x.kind == SCRIPT_UNIT_LITERAL && y.kind == SCRIPT_UNIT_LITERAL ? LITERAL
                                                                          : UNKNOWN;
}
/* `*` and `/`. A literal factor may carry any unit, as `3` in `3 * dt` may
 * be 3 rad/s, so its product is unknown. */
static script_unit product(pass *p, script_range range, script_unit x, script_unit y, int sign)
{
    bool overflow;
    script_unit result = script_unit_product(x, y, sign, &overflow);
    if (overflow) {
        report(p, range, "Unit expression exceeds supported dimensional complexity");
    }
    return result;
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
static script_unit expression_unit(pass *p, int index);
static const script_catalog_capability *call_capability(pass *p, const script_expression *e)
{
    uint64_t id = e->capability;
    if (e->kind == SCRIPT_EXPR_JOIN && e->left >= 0) {
        id = p->a->out->expressions[e->left].type.nominal;
    }
    return script_catalog_capability_by_id(p->a->catalog, id);
}
static script_unit result_unit(pass *p, const script_expression *e, int index)
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
static script_unit call(pass *p, const script_expression *e)
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

static script_unit operation(pass *p, script_range range, script_operator op, int left, int right)
{
    script_unit x = expression_unit(p, left);
    script_unit y = right >= 0 ? expression_unit(p, right) : UNKNOWN;
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
static script_unit member(pass *p, const script_expression *e)
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
static script_unit symbol(pass *p, int index, script_range range)
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
    script_unit initial = expression_unit(p, s->initializer);
    return initial.kind == SCRIPT_UNIT_KNOWN ? initial : UNKNOWN;
}
static script_unit compute(pass *p, const script_expression *e)
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
            script_unit result = expression_unit(p, e->left);
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
                script_unit target = annotated(
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
static script_unit expression_unit(pass *p, int index)
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
static void scale(pass *p, const script_statement *s, script_unit target, script_unit value)
{
    if (target.kind != SCRIPT_UNIT_KNOWN || value.kind != SCRIPT_UNIT_KNOWN ||
        !value.count) {
        return;
    }
    char before[SCRIPT_UNIT_TEXT], after[SCRIPT_UNIT_TEXT];
    script_unit_format(target, before, sizeof before);
    script_unit_format(
        product(p, s->range, target, value, s->op == SCRIPT_OP_MUL ? 1 : -1),
        after,
        sizeof after
    );
    report(p, s->range, "Units differ: %s= would turn %s into %s", symbol_text(s->op), before, after);
}
static void assignment(pass *p, const script_statement *s)
{
    script_unit target = s->expression >= 0 ? expression_unit(p, s->expression)
                       : s->symbol >= 0     ? symbol(p, s->symbol, s->range)
                                            : UNKNOWN;
    script_unit value = expression_unit(p, s->second);
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
            script_unit expected = annotated(&p, s->declaration, s->unit);
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
        script_unit target = expression_unit(&p, b->target);
        script_unit value = expression_unit(&p, b->value);
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
