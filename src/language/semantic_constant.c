#include "language/scalar_constant.h"
#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/scalar.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Admissibility checks the complete graph before short-circuit evaluation. */
const char *script_analyzer_scalar_constant(analyzer *a, int index, pin_type expected, pin_value *out)
{
    int steps = SCRIPT_SCALAR_CONSTANT_STEPS;
    if (!script_analyzer_compile_time_value(a, index, &steps, true)) {
        return "requires bounded compile-time values and operators";
    }
    return script_scalar_evaluate(&a->out->constants, a->out, index, expected, out);
}
/* The outermost integer constant expressions, which fold to one value each;
 * a lone literal or named constant already is one. */
static bool *fold_roots(const script_analysis *analysis)
{
    int count = acount(analysis->expressions);
    bool *constant = xcalloc((size_t)count + 1, sizeof *constant);
    bool *roots = xcalloc((size_t)count + 1, sizeof *roots);
    for (int i = 0; i < count; i++) {
        constant[i] = script_integer_constant(analysis, i);
    }
    for (int i = 0; i < count; i++) {
        const script_expression *e = &analysis->expressions[i];
        roots[i] = constant[i] && e->kind != SCRIPT_EXPR_CONSTANT &&
                   e->kind != SCRIPT_EXPR_SYMBOL;
    }
    for (int i = 0; i < count; i++) {
        const script_expression *e = &analysis->expressions[i];
        if (!constant[i] || e->kind == SCRIPT_EXPR_SYMBOL) {
            continue;
        }
        if (e->left >= 0) {
            roots[e->left] = false;
        }
        if (e->kind == SCRIPT_EXPR_BINARY) {
            roots[e->right] = false;
        }
    }
    free(constant);
    return roots;
}
static void report(analyzer *a, script_range range, const char *format, ...)
{
    script_diagnostic d = { .range = range };
    va_list args;
    va_start(args, format);
    vsnprintf(d.message, sizeof d.message, format, args);
    va_end(args);
    for (int i = 0; i < acount(a->out->diagnostics); i++) {
        const script_diagnostic *old = &a->out->diagnostics[i];
        if (old->range.document == range.document &&
            old->range.start == range.start && !strcmp(old->message, d.message)) {
            return;
        }
    }
    apush(a->out->diagnostics, d);
}
static const char *type_name(script_type type)
{
    type.flags &= ~SCRIPT_TYPE_UNTYPED;
    return script_numeric_name(type);
}
/* A constant operand's exact value, when it has one. */
static bool operand(const script_analysis *analysis, int index, pin_value *out)
{
    script_fold_error error;
    return script_integer_constant(analysis, index) &&
           script_integer_fold(analysis, index, out, &error);
}
static bool integer_storage(script_type type)
{
    return type.storage >= PIN_I8 && type.storage <= PIN_U64;
}
/* Rules that hold wherever a constant operand appears. */
static void check_operation(analyzer *a, const script_expression *e)
{
    const script_analysis *analysis = a->out;
    pin_value value;
    bool integer = integer_storage(e->type);
    if (e->kind == SCRIPT_EXPR_BINARY && integer &&
        (e->op == SCRIPT_OP_DIV || e->op == SCRIPT_OP_MOD) &&
        operand(analysis, e->right, &value) && !behavior_scalar_bits(value)) {
        report(a, e->range, "Constant division by zero");
    }
    if (e->kind == SCRIPT_EXPR_BINARY &&
        (e->op == SCRIPT_OP_SHIFT_LEFT || e->op == SCRIPT_OP_SHIFT_RIGHT) &&
        operand(analysis, e->right, &value) && behavior_scalar_signed(value.type) &&
        (int64_t)behavior_scalar_bits(value) < 0) {
        report(a, analysis->expressions[e->right].range, "Shift count must not be negative");
    }
    if (e->kind == SCRIPT_EXPR_UNARY && e->op == SCRIPT_OP_NEG && integer &&
        !(e->type.flags & SCRIPT_TYPE_UNTYPED) &&
        !behavior_scalar_signed(e->type.storage) &&
        script_integer_constant(analysis, e->left)) {
        report(a, e->range, "Cannot negate an unsigned constant");
    }
}
/* Integer constant expressions evaluate exactly. Each outermost one must fit
 * its type; so must each conversion's operand. */
void script_analyzer_check_constants(analyzer *a)
{
    const script_analysis *analysis = a->out;
    for (int i = 0; i < acount(analysis->expressions); i++) {
        check_operation(a, &analysis->expressions[i]);
    }
    for (int i = 0; i < acount(analysis->statements); i++) {
        const script_statement *s = &analysis->statements[i];
        pin_value value;
        if (s->kind == SCRIPT_STMT_ASSIGN &&
            (s->op == SCRIPT_OP_DIV || s->op == SCRIPT_OP_MOD) &&
            integer_storage(analysis->expressions[s->expression].type) &&
            operand(analysis, s->second, &value) && !behavior_scalar_bits(value)) {
            report(a, s->range, "Constant division by zero");
        }
    }
    bool *roots = fold_roots(analysis);
    for (int i = 0; i < acount(analysis->expressions); i++) {
        pin_value value;
        script_fold_error error;
        if (!roots[i] || script_integer_fold(analysis, i, &value, &error)) {
            continue;
        }
        const script_expression *at = &analysis->expressions[error.expression];
        switch (error.code) {
            case SCRIPT_FOLD_RANGE:
                report(a, at->range, "Constant value is outside the range of %s", type_name(at->type));
                break;
            case SCRIPT_FOLD_SHIFT:
                report(a, at->range, "Constant shift count is too large");
                break;
            case SCRIPT_FOLD_NEGATIVE_SHIFT:
                report(a, at->range, "Shift count must not be negative");
                break;
            case SCRIPT_FOLD_CONVERSION:
                report(
                    a,
                    at->range,
                    "Constant does not fit in %s; use %s(wrap: value)",
                    type_name(at->type),
                    type_name(at->type)
                );
                break;
            default: break;
        }
    }
    free(roots);
}
/* The compiler emits each outermost constant expression's value, not its
 * operations, which would wrap where exact evaluation does not. */
void script_analysis_fold_constants(script_analysis *analysis)
{
    bool *roots = fold_roots(analysis);
    for (int i = 0; i < acount(analysis->expressions); i++) {
        pin_value value;
        script_fold_error error;
        if (!roots[i]) {
            continue;
        }
        bool folded = script_integer_fold(analysis, i, &value, &error);
        assert(folded);
        script_expression *e = &analysis->expressions[i];
        *e = (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                  .type = e->type,
                                  .range = e->range,
                                  .constant = value,
                                  .left = -1,
                                  .right = -1,
                                  .symbol = -1,
                                  .entry = -1 };
    }
    free(roots);
}
