#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned script_analyzer_statement_effects(script_analysis *out, script_statement *statement)
{
    unsigned effects = statement->effects;
    int expressions[] = { statement->expression, statement->second, statement->third };
    for (int i = 0; i < 3; i++) {
        if (expressions[i] >= 0) {
            effects |= out->expressions[expressions[i]].effects;
        }
    }
    if (statement->body >= 0) {
        effects |= out->statements[statement->body].effects;
    }
    if (statement->alternative >= 0) {
        effects |= out->statements[statement->alternative].effects;
    }
    for (int i = 0; i < acount(statement->children); i++) {
        effects |= out->statements[statement->children[i]].effects;
    }
    return effects;
}
/* Only registered component accessors promise identical synchronous execution
 * with and without await. Local callables and ordinary host actions keep their
 * task/completion contract even when their current body has no suspension. */
static bool authored_callable_named(const analyzer *a, TSNode node, const char *name)
{
    if (script_analyzer_is(node, "function_declaration") ||
        script_analyzer_is(node, "macro_declaration") ||
        script_analyzer_is(node, "action_declaration")) {
        if (script_analyzer_text_is(a, script_analyzer_field(node, "name"), name)) {
            return true;
        }
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        if (authored_callable_named(a, ts_node_named_child(node, i), name)) {
            return true;
        }
    }
    return false;
}
bool script_analyzer_synchronous_await(const analyzer *a, TSNode node)
{
    if (!script_analyzer_is(node, "call_expression")) {
        return false;
    }
    TSNode target = script_analyzer_field(node, "function");
    if (!script_analyzer_is(target, "identifier")) {
        return false;
    }
    char *name = script_analyzer_text_copy(a, target);
    /* Task hints precede body declarations. Check authored syntax, rather
     * than the growing symbol table, so this proof is identical in both phases. */
    if (authored_callable_named(a, ts_tree_root_node(a->syntax->tree), name)) {
        free(name);
        return false;
    }
    int index = script_catalog_find_capability(a->catalog, name, SCRIPT_CAPABILITY_FUNCTION);
    if (index == -1) {
        index = script_catalog_find_capability(a->catalog, name, SCRIPT_CAPABILITY_ACTION);
    }
    free(name);
    if (index < 0) {
        return false;
    }
    const script_catalog_capability *cap = &a->catalog->capabilities[index];
    return cap->component_access != SCRIPT_COMPONENT_ACCESS_NONE &&
           !(cap->effects & SCRIPT_EFFECT_SUSPEND) && cap->completion_outcome >= 0;
}
static bool waits(const analyzer *a, TSNode node)
{
    if (script_analyzer_is(node, "yield_statement")) {
        return true;
    }
    if (script_analyzer_is(node, "await_expression")) {
        TSNode value = script_analyzer_field(node, "value");
        while (script_analyzer_is(value, "parenthesized_expression")) {
            value = script_analyzer_field(value, "expression");
        }
        if (!script_analyzer_synchronous_await(a, value)) {
            return true;
        }
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        /* A nested function waits for itself, not for its parent. */
        if (!script_analyzer_is(child, "function_declaration") &&
            !script_analyzer_is(child, "lambda_expression") &&
            !script_analyzer_is(child, "macro_declaration") && waits(a, child)) {
            return true;
        }
    }
    return false;
}
/* A function waits when its body awaits or yields. Calling it without await
 * starts a task, so unawaited calls never make the caller wait. */
bool script_analyzer_function_waits(const analyzer *a, int function)
{
    for (int i = 0; i < acount(a->bodies); i++) {
        if (a->bodies[i].function == function) {
            return waits(a, a->bodies[i].node);
        }
    }
    return false;
}
static bool readonly_function(const analyzer *, int, bool *);
/* Narrowing needs a proof before all bodies have semantic records. This
 * deliberately accepts less than final purity inference: writes, reflected
 * member reads and unresolved/cyclic calls keep the conservative invalidation. */
static bool readonly_syntax(const analyzer *a, TSNode node, bool *active)
{
    if (script_analyzer_is(node, "assignment") ||
        script_analyzer_is(node, "await_expression") ||
        script_analyzer_is(node, "yield_statement") ||
        script_analyzer_is(node, "cancel_statement") ||
        script_analyzer_is(node, "reduce_statement") ||
        script_analyzer_is(node, "state_declaration") ||
        script_analyzer_is(node, "action_declaration") ||
        script_analyzer_is(node, "event_declaration") ||
        script_analyzer_is(node, "bind_declaration") ||
        script_analyzer_is(node, "function_declaration") ||
        script_analyzer_is(node, "lambda_expression") ||
        script_analyzer_is(node, "macro_declaration") ||
        script_analyzer_is(node, "member_expression")) {
        return false;
    }
    if (script_analyzer_is(node, "binary_expression") ||
        script_analyzer_is(node, "unary_expression")) {
        TSNode op = script_analyzer_field(node, "operator");
        const unsigned changes = SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_STRUCTURAL |
                                 SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND;
        for (int i = 0; i < a->catalog->capability_count; i++) {
            const script_catalog_capability *cap = &a->catalog->capabilities[i];
            if (cap->operator_name &&
                script_analyzer_text_is(a, op, cap->operator_name) &&
                (cap->effects & changes)) {
                return false;
            }
        }
    }
    if (script_analyzer_is(node, "call_expression")) {
        TSNode target = script_analyzer_field(node, "function");
        if (!script_analyzer_is(target, "identifier")) {
            return false;
        }
        char *name = script_analyzer_text_copy(a, target);
        int symbol = script_analyzer_callable_symbol(a, name);
        int function = symbol >= 0 ? a->out->symbols[symbol].function : -1;
        if (symbol >= 0 && function < 0 &&
            a->out->symbols[symbol].declaration.start >= 0) {
            free(name);
            return false;
        }
        bool safe;
        if (function >= 0) {
            safe = readonly_function(a, function, active);
        } else {
            int index = script_catalog_find_capability(a->catalog, name, SCRIPT_CAPABILITY_FUNCTION);
            const unsigned changes = SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_STRUCTURAL |
                                     SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND;
            safe = index >= 0 && a->catalog->capabilities[index].pure &&
                   !(a->catalog->capabilities[index].effects & changes);
        }
        free(name);
        if (!safe) {
            return false;
        }
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!readonly_syntax(a, child, active)) {
            return false;
        }
    }
    return true;
}
static bool readonly_function(const analyzer *a, int function, bool *active)
{
    if (active[function]) {
        return false;
    }
    for (int i = 0; i < acount(a->bodies); i++) {
        if (a->bodies[i].function == function) {
            analyzer scope = *a;
            scope.scope = a->out->functions[function].scope;
            active[function] = true;
            bool safe = readonly_syntax(&scope, a->bodies[i].node, active);
            active[function] = false;
            return safe;
        }
    }
    return false;
}
bool script_analyzer_readonly_callable(const analyzer *a, int function)
{
    bool *active = xcalloc((size_t)acount(a->out->functions) + 1, sizeof *active);
    bool safe = readonly_function(a, function, active);
    free(active);
    return safe;
}
enum
{
    QUERY_STARTS_TASK = 1,
    QUERY_READS_INVOCATION = 2,
    QUERY_HIDDEN_CAPTURE = 4
};
static unsigned query_expression_restrictions(const script_analysis *, int, bool *, int);
static unsigned query_statement_restrictions(const script_analysis *out, int index, bool *functions, int function)
{
    if (index < 0) {
        return 0;
    }
    const script_statement *statement = &out->statements[index];
    unsigned
        restrictions = query_expression_restrictions(out, statement->expression, functions, function) |
                       query_expression_restrictions(out, statement->second, functions, function) |
                       query_expression_restrictions(out, statement->third, functions, function) |
                       query_statement_restrictions(out, statement->body, functions, function) |
                       query_statement_restrictions(out, statement->alternative, functions, function);
    for (int i = 0; i < statement->child_count; i++) {
        restrictions |= query_statement_restrictions(out, statement->children[i], functions, function);
    }
    return restrictions;
}
static unsigned query_expression_restrictions(const script_analysis *out, int index, bool *functions, int function)
{
    if (index < 0) {
        return 0;
    }
    const script_expression *value = &out->expressions[index];
    unsigned restrictions = value->task ? QUERY_STARTS_TASK : 0;
    if (value->effects & SCRIPT_EFFECT_INVOCATION_READ) {
        restrictions |= QUERY_READS_INVOCATION;
    }
    if (value->kind == SCRIPT_EXPR_SYMBOL && value->symbol >= 0) {
        const script_symbol *symbol = &out->symbols[value->symbol];
        if (function >= 0 && symbol->scope >= 0 &&
            out->scopes[symbol->scope].function != function &&
            (symbol->kind == SCRIPT_LOCAL || symbol->kind == SCRIPT_PARAMETER ||
             symbol->kind == SCRIPT_RESULT || symbol->kind == SCRIPT_STATE ||
             symbol->kind == SCRIPT_SHARED_STATE || symbol->kind == SCRIPT_COMPONENT ||
             symbol->kind == SCRIPT_QUERY_VARIABLE)) {
            restrictions |= QUERY_HIDDEN_CAPTURE;
        }
        if (symbol->kind == SCRIPT_STATE || symbol->kind == SCRIPT_SHARED_STATE ||
            ((symbol->kind == SCRIPT_PARAMETER || symbol->kind == SCRIPT_LOCAL) &&
             !symbol->query_description && symbol->scope >= 0 &&
             out->scopes[symbol->scope].function < 0)) {
            restrictions |= QUERY_READS_INVOCATION;
        }
    }
    restrictions |= query_expression_restrictions(out, value->left, functions, function);
    if (value->kind == SCRIPT_EXPR_CALL || value->kind == SCRIPT_EXPR_JOIN) {
        if (value->right >= 0 && !functions[value->right]) {
            functions[value->right] = true;
            unsigned called = query_statement_restrictions(
                out,
                out->functions[value->right].body,
                functions,
                value->right
            );
            restrictions |= called;
            if (called & QUERY_READS_INVOCATION) {
                restrictions |= QUERY_HIDDEN_CAPTURE;
            }
        }
    } else {
        restrictions |= query_expression_restrictions(out, value->right, functions, function);
    }
    for (int i = 0; i < value->argument_count; i++) {
        restrictions |= query_expression_restrictions(out, value->arguments[i].value, functions, function);
    }
    return restrictions;
}
typedef struct
{
    analyzer *a;
    uint64_t component;
    bool *functions;
    bool access, opaque, task;
} reduction_check;
static void reduction_capability(reduction_check *check, uint64_t id)
{
    const script_catalog_capability
        *cap = script_catalog_capability_by_id(check->a->catalog, id);
    if (!cap) {
        return;
    }
    if (cap->component_access != SCRIPT_COMPONENT_ACCESS_NONE) {
        if (cap->component_access != SCRIPT_COMPONENT_ACCESS_HAS) {
            check->access |= cap->component == check->component;
            check->opaque |= !cap->component;
        }
        return;
    }
    if (cap->state_access == SCRIPT_STATE_ACCESS_NONE) {
        check->opaque |= (cap->effects &
                          (SCRIPT_EFFECT_READ | SCRIPT_EFFECT_WRITE |
                           SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_STRUCTURAL)) != 0;
    }
}
static void reduction_expression(reduction_check *, int, bool);
static void reduction_statement(reduction_check *check, int index)
{
    if (index < 0) {
        return;
    }
    const script_analysis *out = check->a->out;
    const script_statement *s = &out->statements[index];
    if (s->kind == SCRIPT_STMT_REDUCE) {
        /* Locating the target does not read its value. Its entity expression
         * can still read component storage and must be checked. */
        int base = s->expression;
        while (base >= 0 && out->expressions[base].kind == SCRIPT_EXPR_MEMBER) {
            base = out->expressions[base].left;
        }
        if (base >= 0 && out->expressions[base].kind == SCRIPT_EXPR_COMPONENT) {
            reduction_expression(check, out->expressions[base].left, false);
        }
    } else {
        reduction_expression(check, s->expression, s->kind == SCRIPT_STMT_IF || s->kind == SCRIPT_STMT_WHILE);
    }
    reduction_expression(check, s->second, false);
    reduction_expression(check, s->third, false);
    reduction_statement(check, s->body);
    reduction_statement(check, s->alternative);
    for (int i = 0; i < acount(s->children); i++) {
        reduction_statement(check, s->children[i]);
    }
}
static void reduction_expression(reduction_check *check, int index, bool condition)
{
    if (index < 0) {
        return;
    }
    const script_analysis *out = check->a->out;
    const script_expression *e = &out->expressions[index];
    if (e->read) {
        reduction_capability(check, e->read);
    }
    if (e->write) {
        reduction_capability(check, e->write);
    }
    bool membership = condition && (e->type.flags & SCRIPT_TYPE_OPTIONAL);
    if (!membership) {
        check->access |= e->kind == SCRIPT_EXPR_COMPONENT &&
                         e->capability == check->component;
        check->access |= e->kind == SCRIPT_EXPR_DEREFERENCE &&
                         script_analyzer_component_target(out, index) == check->component;
        check->access |= e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 &&
                         out->symbols[e->symbol].kind == SCRIPT_COMPONENT &&
                         out->symbols[e->symbol].type.storage != PIN_POINTER &&
                         out->symbols[e->symbol].component_type.nominal ==
                             check->component;
    }
    check->task |= e->task;
    bool predicate = (e->kind == SCRIPT_EXPR_UNARY && e->op == SCRIPT_OP_NOT) ||
                     (e->kind == SCRIPT_EXPR_BINARY &&
                      (e->op == SCRIPT_OP_AND || e->op == SCRIPT_OP_OR));
    if (e->kind == SCRIPT_EXPR_BINARY &&
        (e->op == SCRIPT_OP_EQ || e->op == SCRIPT_OP_NE)) {
        predicate |= (e->left >= 0 && out->expressions[e->left].kind == SCRIPT_EXPR_NIL) ||
                     (e->right >= 0 && out->expressions[e->right].kind == SCRIPT_EXPR_NIL);
    }
    reduction_expression(check, e->left, predicate);
    if (e->kind == SCRIPT_EXPR_CALL || e->kind == SCRIPT_EXPR_JOIN) {
        if (e->right >= 0 && !check->functions[e->right]) {
            check->functions[e->right] = true;
            reduction_statement(check, out->functions[e->right].body);
        }
        if (e->right < 0) {
            reduction_capability(check, e->capability);
        }
    } else {
        reduction_expression(check, e->right, predicate);
    }
    for (int i = 0; i < acount(e->arguments); i++) {
        int argument = e->arguments[i].value;
        if (e->kind == SCRIPT_EXPR_CALL && argument >= 0 &&
            out->expressions[argument].type.storage == PIN_POINTER) {
            check->access |= script_analyzer_component_target(out, argument) ==
                             check->component;
        }
        reduction_expression(check, argument, false);
    }
}
static void finish_reductions(analyzer *a)
{
    script_analysis *out = a->out;
    uint64_t *checked = NULL;
    for (int i = 0; i < acount(out->statements); i++) {
        const script_statement *s = &out->statements[i];
        if (s->kind != SCRIPT_STMT_REDUCE) {
            continue;
        }
        uint64_t component = script_analyzer_component_target(out, s->expression);
        bool seen = false;
        for (int j = 0; j < acount(checked); j++) {
            seen |= checked[j] == component;
        }
        if (!component || seen) {
            continue;
        }
        apush(checked, component);
        bool unordered = s->reduction == SCRIPT_REDUCTION_ADD_UNORDERED;
        for (int j = i + 1; j < acount(out->statements); j++) {
            const script_statement *other = &out->statements[j];
            if (other->kind == SCRIPT_STMT_REDUCE &&
                script_analyzer_component_target(out, other->expression) == component &&
                (other->reduction == SCRIPT_REDUCTION_ADD_UNORDERED) != unordered) {
                script_diagnostic d = { .range = other->range };
                snprintf(d.message, sizeof d.message, "A component cannot mix ordered and unordered reductions in one system");
                apush(out->diagnostics, d);
                break;
            }
        }
        reduction_check check = {
            .a = a,
            .component = component,
            .functions = xcalloc((size_t)acount(out->functions) + 1, sizeof(bool))
        };
        bool suspend = false;
        for (int j = 0; j < acount(out->functions); j++) {
            const script_function *f = &out->functions[j];
            if (f->kind == SCRIPT_FUNCTION_SYSTEM) {
                suspend |= (f->effects & SCRIPT_EFFECT_SUSPEND) != 0;
                reduction_statement(&check, f->body);
            }
        }
        const script_catalog_type *type = script_catalog_type_by_id(a->catalog, component);
        for (int j = 0; j < acount(out->descriptions); j++) {
            const script_query_description *description = &out->descriptions[j];
            if (!description->system) {
                continue;
            }
            for (int k = 0; k < acount(description->stages); k++) {
                const script_query_stage *stage = &description->stages[k];
                if (stage->symbol >= 0) {
                    reduction_expression(&check, stage->expression, false);
                }
            }
            for (int k = 0; k < acount(description->symbols); k++) {
                const script_symbol *binding = &out->symbols[description->symbols[k]];
                check.access |= binding->kind == SCRIPT_COMPONENT &&
                                binding->type.storage == PIN_STRUCT &&
                                binding->component_type.nominal == component;
            }
        }
        for (int j = 0; j < acount(out->queries); j++) {
            const script_query *q = &out->queries[j];
            if (q->kind == SCRIPT_QUERY_VALUE) {
                reduction_expression(&check, q->value, true);
            }
            check.access |= q->kind == SCRIPT_QUERY_CHANGED && type &&
                            q->component == type->component_id;
        }
        for (int j = 0; j < acount(out->bindings); j++) {
            reduction_expression(&check, out->bindings[j].target, false);
            reduction_expression(&check, out->bindings[j].value, false);
        }
        if (suspend || check.task) {
            script_diagnostic d = { .range = s->range };
            snprintf(d.message, sizeof d.message, "Reduction requires a synchronous system body without tasks");
            apush(out->diagnostics, d);
        }
        if (check.access || check.opaque) {
            script_diagnostic d = { .range = s->range };
            if (check.opaque) {
                snprintf(d.message, sizeof d.message, "Reduction producer cannot call an opaque component accessor");
            } else {
                snprintf(
                    d.message,
                    sizeof d.message,
                    "Reduction producer cannot read or write component '%s' "
                    "outside reductions",
                    type ? type->name : "unknown"
                );
            }
            apush(out->diagnostics, d);
        }
        free(check.functions);
    }
    afree(checked);
}
static void finish_reference_iteration(analyzer *a)
{
    const script_analysis *out = a->out;
    for (int i = 0; i < acount(out->statements); i++) {
        const script_statement *loop = &out->statements[i];
        if (loop->kind != SCRIPT_STMT_FOREACH || loop->symbol < 0 ||
            !(out->symbols[loop->symbol].type.flags & SCRIPT_TYPE_REFERENCE) ||
            loop->body < 0) {
            continue;
        }
        const script_statement *body = &out->statements[loop->body];
        const script_expression *source = &out->expressions[loop->expression];
        bool unsafe = (body->effects & (SCRIPT_EFFECT_SUSPEND | SCRIPT_EFFECT_HOST |
                                        SCRIPT_EFFECT_STRUCTURAL)) != 0;
        for (int j = 0; j < acount(out->expressions); j++) {
            const script_expression *e = &out->expressions[j];
            if (e->range.start < body->range.start || e->range.end > body->range.end) {
                continue;
            }
            /* While borrowed, the source can only be reached through item.
             * Other bindings hold snapshots, not aliases to its mutable bank. */
            unsafe |= e->kind == SCRIPT_EXPR_SYMBOL && e->symbol == source->symbol;
            unsafe |= e->kind == SCRIPT_EXPR_CALL &&
                      (e->effects & SCRIPT_EFFECT_WRITE);
        }
        if (unsafe) {
            script_diagnostic d = { .range = loop->range };
            snprintf(d.message, sizeof d.message, "Reference iteration cannot suspend, start tasks, call mutating procedures or access its borrowed source binding");
            apush(a->out->diagnostics, d);
        }
    }
}
void script_analyzer_finish_effects(analyzer *a)
{
    script_analysis *out = a->out;
    for (int pass = 0; pass <= acount(out->functions); pass++) {
        bool changed = false;
        for (int i = 0; i < acount(out->expressions); i++) {
            script_expression *expression = &out->expressions[i];
            unsigned effects = expression->effects;
            if (expression->kind == SCRIPT_EXPR_SYMBOL && expression->symbol >= 0) {
                const script_symbol *symbol = &out->symbols[expression->symbol];
                if (symbol->kind == SCRIPT_STATE || symbol->kind == SCRIPT_SHARED_STATE ||
                    ((symbol->kind == SCRIPT_PARAMETER || symbol->kind == SCRIPT_LOCAL) &&
                     !symbol->query_description && symbol->scope >= 0 &&
                     out->scopes[symbol->scope].function < 0)) {
                    effects |= SCRIPT_EFFECT_INVOCATION_READ;
                }
            }
            if (expression->task) {
                /* Starting an activation changes execution ownership even if
                 * the callee's only body effect is its own suspension. */
                effects |= SCRIPT_EFFECT_HOST;
            }
            if (expression->left >= 0) {
                effects |= out->expressions[expression->left].effects;
            }
            if (expression->kind == SCRIPT_EXPR_CALL ||
                expression->kind == SCRIPT_EXPR_JOIN) {
                if (expression->right >= 0) {
                    effects |= out->functions[expression->right].effects &
                               ~(expression->task ? SCRIPT_EFFECT_SUSPEND : 0u);
                }
            } else if (expression->right >= 0) {
                effects |= out->expressions[expression->right].effects;
            }
            for (int j = 0; j < acount(expression->arguments); j++) {
                effects |= out->expressions[expression->arguments[j].value].effects;
            }
            changed |= effects != expression->effects;
            expression->effects = effects;
        }
        for (int i = 0; i < acount(out->statements); i++) {
            unsigned effects = script_analyzer_statement_effects(out, &out->statements[i]);
            changed |= effects != out->statements[i].effects;
            out->statements[i].effects = effects;
        }
        for (int i = 0; i < acount(out->functions); i++) {
            script_function *function = &out->functions[i];
            unsigned effects = function->body >= 0
                                 ? out->statements[function->body].effects
                                 : 0;
            for (int j = 0; j < acount(out->functions); j++) {
                if (out->functions[j].parent == i) {
                    effects |= out->functions[j].effects;
                }
            }
            changed |= effects != function->effects;
            function->effects = effects;
        }
        if (!changed) {
            break;
        }
    }
    finish_reductions(a);
    finish_reference_iteration(a);
    const unsigned impure = SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_STRUCTURAL |
                            SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND |
                            SCRIPT_EFFECT_RANDOM;
    for (int i = 0; i < acount(out->queries); i++) {
        const script_query *query = &out->queries[i];
        if (query->kind != SCRIPT_QUERY_VALUE) {
            continue;
        }
        bool *functions = xcalloc((size_t)(acount(out->functions) + 1), sizeof *functions);
        unsigned restrictions = query_expression_restrictions(out, query->value, functions, -1);
        free(functions);
        if ((out->expressions[query->value].effects & impure) ||
            (restrictions & QUERY_STARTS_TASK)) {
            script_diagnostic d = { .range = query->range };
            snprintf(d.message, sizeof d.message, "Query predicate requires a deterministic value without side effects or suspension");
            apush(out->diagnostics, d);
        }
        if (restrictions & QUERY_HIDDEN_CAPTURE) {
            script_diagnostic d = { .range = query->range };
            snprintf(d.message, sizeof d.message, "Pass captured values explicitly to query helper functions");
            apush(out->diagnostics, d);
        }
        if ((restrictions & QUERY_READS_INVOCATION) &&
            (!query->description || out->descriptions[query->description - 1].system)) {
            script_diagnostic d = { .range = query->range };
            snprintf(d.message, sizeof d.message, "Query predicate cannot read system state, body locals or event payload; test invocation values in the system body");
            apush(out->diagnostics, d);
        }
    }
    for (int i = 0; i < acount(out->descriptions); i++) {
        const script_query_description *query = &out->descriptions[i];
        for (int j = 0; j < acount(query->stages); j++) {
            const script_query_stage *stage = &query->stages[j];
            if (stage->symbol < 0 || stage->expression < 0) {
                continue;
            }
            bool *functions = xcalloc((size_t)(acount(out->functions) + 1), sizeof *functions);
            unsigned restrictions = query_expression_restrictions(out, stage->expression, functions, -1);
            free(functions);
            if (restrictions & QUERY_HIDDEN_CAPTURE) {
                script_diagnostic diagnostic = { .range = stage->range };
                snprintf(diagnostic.message, sizeof diagnostic.message, "Pass captured values explicitly to query helper functions");
                apush(out->diagnostics, diagnostic);
            }
            if (query->system && (restrictions & QUERY_READS_INVOCATION)) {
                script_diagnostic diagnostic = { .range = stage->range };
                snprintf(diagnostic.message, sizeof diagnostic.message, "Query binding cannot read system state, body locals or event payload");
                apush(out->diagnostics, diagnostic);
            }
            if ((out->expressions[stage->expression].effects & impure) ||
                (restrictions & QUERY_STARTS_TASK)) {
                script_diagnostic diagnostic = { .range = stage->range };
                snprintf(diagnostic.message, sizeof diagnostic.message, "Query binding requires a deterministic value without side effects or suspension");
                apush(out->diagnostics, diagnostic);
            }
        }
    }
    for (int i = 0; i < acount(out->statements); i++) {
        const script_statement *s = &out->statements[i];
        if (s->kind == SCRIPT_STMT_FOREACH &&
            out->expressions[s->expression].kind == SCRIPT_EXPR_RELATION &&
            (out->statements[s->body].effects &
             (SCRIPT_EFFECT_SUSPEND | SCRIPT_EFFECT_STRUCTURAL))) {
            script_diagnostic d = { .range = s->range };
            snprintf(d.message, sizeof d.message, "Streaming relationship iteration cannot suspend or change entity structure");
            apush(out->diagnostics, d);
        }
    }
    for (int i = 0; i < acount(out->functions); i++) {
        script_function *function = &out->functions[i];
        bool *functions = xcalloc((size_t)acount(out->functions) + 1, sizeof *functions);
        functions[i] = true;
        unsigned restrictions = query_statement_restrictions(out, function->body, functions, i);
        free(functions);
        function->pure = function->body >= 0 && !(function->effects & impure) &&
                         !(restrictions & QUERY_STARTS_TASK);
    }
    for (int i = 0; i < acount(out->bindings); i++) {
        script_binding *binding = &out->bindings[i];
        if (out->expressions[binding->value].effects & impure) {
            script_diagnostic d = { .range = binding->range };
            snprintf(d.message, sizeof d.message, "Binding requires a deterministic value without side effects or suspension");
            apush(out->diagnostics, d);
        }
    }
    for (int i = 0; i < acount(out->symbols); i++) {
        script_symbol *symbol = &out->symbols[i];
        if ((symbol->kind == SCRIPT_STATE || symbol->kind == SCRIPT_SHARED_STATE) &&
            symbol->initializer >= 0 && out->expressions[symbol->initializer].effects) {
            script_diagnostic d = { .range = symbol->declaration };
            snprintf(d.message, sizeof d.message, "State default depends on runtime effects");
            apush(out->diagnostics, d);
        }
    }
}
bool script_analyzer_binding_depends(const script_binding *reader, const script_binding *writer)
{
    for (int i = 0; i < acount(reader->reads); i++) {
        for (int j = 0; j < acount(writer->writes); j++) {
            if (reader->reads[i] == writer->writes[j]) {
                return true;
            }
        }
    }
    return false;
}
void script_analyzer_finish_bindings(analyzer *a)
{
    int count = acount(a->out->bindings);
    bool *emitted = xcalloc((size_t)(count ? count : 1), sizeof *emitted);
    script_binding *ordered = NULL;
    for (int pass = 0; pass < count; pass++) {
        int found = -1;
        for (int i = 0; i < count; i++) {
            if (emitted[i]) {
                continue;
            }
            bool ready = true;
            for (int j = 0; j < count; j++) {
                if (!emitted[j] && script_analyzer_binding_depends(
                                       &a->out->bindings[i],
                                       &a->out->bindings[j]
                                   )) {
                    ready = false;
                    break;
                }
            }
            if (ready) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            script_diagnostic d = { .range = a->out->bindings[0].range };
            snprintf(d.message, sizeof d.message, "Binding dependency cycle");
            apush(a->out->diagnostics, d);
            afree(ordered);
            free(emitted);
            return;
        }
        emitted[found] = true;
        apush(ordered, a->out->bindings[found]);
    }
    free(emitted);
    afree(a->out->bindings);
    a->out->bindings = ordered;
}
static void define_inferred(analyzer *a, int body);
void script_analyzer_callable_definition(analyzer *a, int index, TSNode body)
{
    int old_scope = a->scope, old_function = a->function;
    a->function = index;
    a->scope = a->out->functions[index].scope;
    if (a->out->functions[index].kind != SCRIPT_FUNCTION_SYSTEM) {
        int nested = acount(a->bodies);
        script_analyzer_declaration_pass(a, body);
        /* Nested inferred functions have results before this body calls them. */
        for (int i = nested; i < acount(a->bodies); i++) {
            if (a->out->functions[a->bodies[i].function].inferred) {
                define_inferred(a, i);
            }
        }
        a->scope = a->out->functions[index].scope;
        a->function = index;
        script_analyzer_definition_pass(a, body);
    }
    script_statement block = { .kind = SCRIPT_STMT_BLOCK,
                               .scope = a->scope,
                               .range = script_analyzer_range(a, body),
                               .symbol = -1,
                               .expression = -1,
                               .second = -1,
                               .third = -1,
                               .body = -1,
                               .alternative = -1 };
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (ts_node_is_extra(node) || script_analyzer_declaration_kind(node)) {
            continue;
        }
        int child = script_analyzer_statement(a, node);
        apush(block.children, child);
        block.effects |= a->out->statements[child].effects;
    }
    a->out->functions[index].body = script_analyzer_statement_add(a, block);
    a->out->functions[index].effects = block.effects;
    script_analyzer_union_results(a, index, body);
    a->scope = old_scope;
    a->function = old_function;
    asetlen(a->narrowed, 0);
}
/* Whether `node` calls `symbol`, resolving each call where `node` sees it. */
static bool calls(analyzer *a, TSNode node, int symbol)
{
    if (script_analyzer_is(node, "call_expression")) {
        /* Spelled as call analysis resolves it, so `Lib.F` is not `F`. */
        char *target = script_analyzer_qualified_name(a, script_analyzer_field(node, "function"));
        int resolved = script_analyzer_import_namespace(a, node, target)
                         ? script_analyzer_package_function(a, node, target)
                         : script_analyzer_callable_symbol(a, target);
        bool match = resolved == symbol;
        free(target);
        if (match) {
            return true;
        }
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!script_analyzer_is(child, "function_declaration") &&
            !script_analyzer_is(child, "lambda_expression") &&
            !script_analyzer_is(child, "macro_declaration") &&
            calls(a, child, symbol)) {
            return true;
        }
    }
    return false;
}
/* Defines inferred bodies after the inferred functions they call, so each
 * call sees its callee's results. */
static void define_inferred(analyzer *a, int body)
{
    function_body b = a->bodies[body];
    if (b.state == BODY_DONE) {
        return;
    }
    if (b.state == BODY_ACTIVE) {
        TSNode name = script_analyzer_field(ts_node_parent(b.node), "name");
        script_analyzer_diagnostic(a, name, "Recursive functions need declared results");
        return;
    }
    a->bodies[body].state = BODY_ACTIVE;
    for (int i = 0; i < acount(a->bodies); i++) {
        const script_function *callee = &a->out->functions[a->bodies[i].function];
        int old_scope = a->scope;
        a->scope = a->out->functions[b.function].scope;
        bool called = callee->inferred && calls(a, b.node, callee->symbol);
        a->scope = old_scope;
        if (called) {
            define_inferred(a, i);
        }
    }
    script_analyzer_callable_definition(a, b.function, b.node);
    script_analyzer_function_type(a, b.function);
    a->bodies[body].state = BODY_DONE;
}
/* Defines the callable bodies not yet defined, or only the inferred ones. */
void script_analyzer_callable_definitions(analyzer *a, bool inferred_only)
{
    for (int i = 0; i < acount(a->bodies); i++) {
        if (a->out->functions[a->bodies[i].function].inferred) {
            define_inferred(a, i);
        }
    }
    for (int i = 0; i < acount(a->bodies) && !inferred_only; i++) {
        if (a->bodies[i].state == BODY_PENDING) {
            a->bodies[i].state = BODY_DONE;
            script_analyzer_callable_definition(
                a,
                a->bodies[i].function,
                a->bodies[i].node
            );
        }
    }
}
void script_analyzer_host_declarations(analyzer *a, TSNode root)
{
    const script_catalog *visible = a->native ? a->native : a->catalog;
    for (int i = 0; i < visible->global_count; i++) {
        const script_catalog_global *global = &visible->globals[i];
        apush(
            a->out->symbols,
            ((script_symbol){ .id = global->id,
                              .name = xstrdup(global->name),
                              .kind = SCRIPT_GLOBAL,
                              .type = global->type,
                              .scope = 0,
                              .initializer = -1,
                              .function = -1,
                              .writable = global->writable,
                              .declaration = { .start = -1, .end = -1 } })
        );
    }
    for (int i = 0; i < visible->capability_count; i++) {
        const script_catalog_capability *capability = &visible->capabilities[i];
        if (capability->kind == SCRIPT_CAPABILITY_PROTOTYPE) {
            continue;
        }
        script_type type = capability->result_count == 1
                             ? capability->results[0].type
                             : (script_type){ .storage = capability->result_count
                                                           ? PIN_STRUCT
                                                           : PIN_FLOW,
                                              .nominal = capability->result_count
                                                           ? capability->id
                                                           : 0,
                                              .flags = capability->result_count
                                                         ? SCRIPT_TYPE_RESULTS
                                                         : 0 };
        apush(
            a->out->symbols,
            ((script_symbol){ .id = capability->id,
                              .capability = capability->id,
                              .name = xstrdup(
                                  capability->qualified_name ? capability->qualified_name
                                                             : capability->name
                              ),
                              .kind = capability->kind == SCRIPT_CAPABILITY_EVENT ? SCRIPT_EVENT
                                    : capability->kind == SCRIPT_CAPABILITY_ACTION
                                        ? SCRIPT_ACTION
                                        : SCRIPT_FUNCTION,
                              .type = type,
                              .scope = 0,
                              .initializer = -1,
                              .function = -1,
                              .declaration = { .start = -1, .end = -1 } })
        );
    }
    (void)root;
}
