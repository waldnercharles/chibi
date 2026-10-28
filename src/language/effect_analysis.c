#include "language/effect_analysis.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t component_target(const script_effect_facts *out, int index)
{
    int remaining = out->expression_count;
    while (index >= 0 && remaining--) {
        const script_expression *e = &out->expressions[index];
        if (e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_DEREFERENCE) {
            if (e->read || e->write || e->type.container != PIN_CONTAINER_SINGLE) {
                return 0;
            }
            index = e->left;
            continue;
        }
        if (e->kind == SCRIPT_EXPR_COMPONENT) {
            return e->capability;
        }
        if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0) {
            const script_symbol *symbol = &out->symbols[e->symbol];
            if (symbol->kind == SCRIPT_COMPONENT) {
                return symbol->component_type.nominal;
            }
            if (symbol->kind == SCRIPT_LOCAL && symbol->type.storage == PIN_POINTER &&
                !symbol->mutations && symbol->initializer >= 0) {
                index = symbol->initializer;
                continue;
            }
        }
        return 0;
    }
    return 0;
}
static unsigned statement_effects(const script_effect_facts *out, const script_statement *s)
{
    unsigned effects = s->effects;
    int es[] = { s->expression, s->second, s->third };
    for (int i = 0; i < 3; i++) {
        if (es[i] >= 0) {
            effects |= out->expressions[es[i]].effects;
        }
    }
    if (s->body >= 0) {
        effects |= out->statements[s->body].effects;
    }
    if (s->alternative >= 0) {
        effects |= out->statements[s->alternative].effects;
    }
    for (int i = 0; i < s->child_count; i++) {
        effects |= out->statements[s->children[i]].effects;
    }
    return effects;
}
enum
{
    QUERY_STARTS_TASK = 1,
    QUERY_READS_INVOCATION = 2,
    QUERY_HIDDEN_CAPTURE = 4
};
static unsigned query_expression_restrictions(const script_effect_facts *, int, bool *, int);
static unsigned query_statement_restrictions(
    const script_effect_facts *out,
    int index,
    bool *functions,
    int function
)
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
static unsigned query_expression_restrictions(
    const script_effect_facts *out,
    int index,
    bool *functions,
    int function
)
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
    const script_effect_facts *a;
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
    const script_effect_facts *out = check->a;
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
    for (int i = 0; i < s->child_count; i++) {
        reduction_statement(check, s->children[i]);
    }
}
static void reduction_expression(reduction_check *check, int index, bool condition)
{
    if (index < 0) {
        return;
    }
    const script_effect_facts *out = check->a;
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
                         component_target(out, index) == check->component;
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
    for (int i = 0; i < e->argument_count; i++) {
        int argument = e->arguments[i].value;
        if (e->kind == SCRIPT_EXPR_CALL && argument >= 0 &&
            out->expressions[argument].type.storage == PIN_POINTER) {
            check->access |= component_target(out, argument) == check->component;
        }
        reduction_expression(check, argument, false);
    }
}
static void finish_reductions(const script_effect_facts *a, script_diagnostic **diagnostics)
{
    const script_effect_facts *out = a;
    uint64_t *checked = NULL;
    for (int i = 0; i < out->statement_count; i++) {
        const script_statement *s = &out->statements[i];
        if (s->kind != SCRIPT_STMT_REDUCE) {
            continue;
        }
        uint64_t component = component_target(out, s->expression);
        bool seen = false;
        for (int j = 0; j < acount(checked); j++) {
            seen |= checked[j] == component;
        }
        if (!component || seen) {
            continue;
        }
        apush(checked, component);
        bool unordered = s->reduction == SCRIPT_REDUCTION_ADD_UNORDERED;
        for (int j = i + 1; j < out->statement_count; j++) {
            const script_statement *other = &out->statements[j];
            if (other->kind == SCRIPT_STMT_REDUCE &&
                component_target(out, other->expression) == component &&
                (other->reduction == SCRIPT_REDUCTION_ADD_UNORDERED) != unordered) {
                script_diagnostic d = { .range = other->range };
                snprintf(d.message, sizeof d.message, "A component cannot mix ordered and unordered reductions in one system");
                apush(*diagnostics, d);
                break;
            }
        }
        reduction_check check = {
            .a = a,
            .component = component,
            .functions = xcalloc((size_t)out->function_count + 1, sizeof(bool))
        };
        bool suspend = false;
        for (int j = 0; j < out->function_count; j++) {
            const script_function *f = &out->functions[j];
            if (f->kind == SCRIPT_FUNCTION_SYSTEM) {
                suspend |= (f->effects & SCRIPT_EFFECT_SUSPEND) != 0;
                reduction_statement(&check, f->body);
            }
        }
        const script_catalog_type *type = script_catalog_type_by_id(a->catalog, component);
        for (int j = 0; j < out->description_count; j++) {
            const script_query_description *description = &out->descriptions[j];
            if (!description->system) {
                continue;
            }
            for (int k = 0; k < acount(description->stages); k++) {
                const script_query_stage *stage = &description->stages[k];
                if (stage->symbol >= 0 && stage->expression >= 0) {
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
        for (int j = 0; j < out->query_count; j++) {
            const script_query *q = &out->queries[j];
            if (q->kind == SCRIPT_QUERY_VALUE) {
                reduction_expression(&check, q->value, true);
            }
            check.access |= q->kind == SCRIPT_QUERY_CHANGED && type &&
                            q->component == type->component_id;
        }
        for (int j = 0; j < out->binding_count; j++) {
            reduction_expression(&check, out->bindings[j].target, false);
            reduction_expression(&check, out->bindings[j].value, false);
        }
        if (suspend || check.task) {
            script_diagnostic d = { .range = s->range };
            snprintf(d.message, sizeof d.message, "Reduction requires a synchronous system body without tasks");
            apush(*diagnostics, d);
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
            apush(*diagnostics, d);
        }
        free(check.functions);
    }
    afree(checked);
}
static void finish_reference_iteration(const script_effect_facts *out, script_diagnostic **diagnostics)
{
    for (int i = 0; i < out->statement_count; i++) {
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
        for (int j = 0; j < out->expression_count; j++) {
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
            apush(*diagnostics, d);
        }
    }
}
static void finish_effects(script_effect_facts *a, script_diagnostic **diagnostics)
{
    script_effect_facts *out = a;
    for (int pass = 0; pass <= out->function_count; pass++) {
        bool changed = false;
        for (int i = 0; i < out->expression_count; i++) {
            script_expression *expression = &((script_expression *)out->expressions)[i];
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
            for (int j = 0; j < expression->argument_count; j++) {
                effects |= out->expressions[expression->arguments[j].value].effects;
            }
            changed |= effects != expression->effects;
            expression->effects = effects;
        }
        for (int i = 0; i < out->statement_count; i++) {
            unsigned effects = statement_effects(out, &out->statements[i]);
            changed |= effects != out->statements[i].effects;
            ((script_statement *)out->statements)[i].effects = effects;
        }
        for (int i = 0; i < out->function_count; i++) {
            script_function *function = &((script_function *)out->functions)[i];
            unsigned effects = function->body >= 0
                                 ? out->statements[function->body].effects
                                 : 0;
            for (int j = 0; j < out->function_count; j++) {
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
    finish_reductions(a, diagnostics);
    finish_reference_iteration(out, diagnostics);
    const unsigned impure = SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_STRUCTURAL |
                            SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND |
                            SCRIPT_EFFECT_RANDOM;
    for (int i = 0; i < out->query_count; i++) {
        const script_query *query = &out->queries[i];
        if (query->kind != SCRIPT_QUERY_VALUE) {
            continue;
        }
        bool *functions = xcalloc((size_t)(out->function_count + 1), sizeof *functions);
        unsigned restrictions = query_expression_restrictions(out, query->value, functions, -1);
        free(functions);
        if ((out->expressions[query->value].effects & impure) ||
            (restrictions & QUERY_STARTS_TASK)) {
            script_diagnostic d = { .range = query->range };
            snprintf(d.message, sizeof d.message, "Query predicate requires a deterministic value without side effects or suspension");
            apush(*diagnostics, d);
        }
        if (restrictions & QUERY_HIDDEN_CAPTURE) {
            script_diagnostic d = { .range = query->range };
            snprintf(d.message, sizeof d.message, "Pass captured values explicitly to query helper functions");
            apush(*diagnostics, d);
        }
        if ((restrictions & QUERY_READS_INVOCATION) &&
            (!query->description || out->descriptions[query->description - 1].system)) {
            script_diagnostic d = { .range = query->range };
            snprintf(d.message, sizeof d.message, "Query predicate cannot read system state, body locals or event payload; test invocation values in the system body");
            apush(*diagnostics, d);
        }
    }
    for (int i = 0; i < out->description_count; i++) {
        const script_query_description *query = &out->descriptions[i];
        for (int j = 0; j < acount(query->stages); j++) {
            const script_query_stage *stage = &query->stages[j];
            if (stage->symbol < 0 || stage->expression < 0) {
                continue;
            }
            bool *functions = xcalloc((size_t)(out->function_count + 1), sizeof *functions);
            unsigned restrictions = query_expression_restrictions(out, stage->expression, functions, -1);
            free(functions);
            if (restrictions & QUERY_HIDDEN_CAPTURE) {
                script_diagnostic diagnostic = { .range = stage->range };
                snprintf(diagnostic.message, sizeof diagnostic.message, "Pass captured values explicitly to query helper functions");
                apush(*diagnostics, diagnostic);
            }
            if (query->system && (restrictions & QUERY_READS_INVOCATION)) {
                script_diagnostic diagnostic = { .range = stage->range };
                snprintf(diagnostic.message, sizeof diagnostic.message, "Query binding cannot read system state, body locals or event payload");
                apush(*diagnostics, diagnostic);
            }
            if ((out->expressions[stage->expression].effects & impure) ||
                (restrictions & QUERY_STARTS_TASK)) {
                script_diagnostic diagnostic = { .range = stage->range };
                snprintf(diagnostic.message, sizeof diagnostic.message, "Query binding requires a deterministic value without side effects or suspension");
                apush(*diagnostics, diagnostic);
            }
        }
    }
    for (int i = 0; i < out->statement_count; i++) {
        const script_statement *s = &out->statements[i];
        if (s->kind == SCRIPT_STMT_FOREACH &&
            out->expressions[s->expression].kind == SCRIPT_EXPR_RELATION &&
            (out->statements[s->body].effects &
             (SCRIPT_EFFECT_SUSPEND | SCRIPT_EFFECT_STRUCTURAL))) {
            script_diagnostic d = { .range = s->range };
            snprintf(d.message, sizeof d.message, "Streaming relationship iteration cannot suspend or change entity structure");
            apush(*diagnostics, d);
        }
    }
    for (int i = 0; i < out->binding_count; i++) {
        const script_binding *binding = &out->bindings[i];
        if (out->expressions[binding->value].effects & impure) {
            script_diagnostic d = { .range = binding->range };
            snprintf(d.message, sizeof d.message, "Binding requires a deterministic value without side effects or suspension");
            apush(*diagnostics, d);
        }
    }
    for (int i = 0; i < out->symbol_count; i++) {
        const script_symbol *symbol = &out->symbols[i];
        if ((symbol->kind == SCRIPT_STATE || symbol->kind == SCRIPT_SHARED_STATE) &&
            symbol->initializer >= 0 && out->expressions[symbol->initializer].effects) {
            script_diagnostic d = { .range = symbol->declaration };
            snprintf(d.message, sizeof d.message, "State default depends on runtime effects");
            apush(*diagnostics, d);
        }
    }
}

static void *copy(arena *storage, const void *source, size_t size)
{
    void *result = arena_alloc(storage, size);
    if (size) {
        memcpy(result, source, size);
    }
    return result;
}
script_effect_result script_effect_analyze(arena *storage, const script_effect_facts *facts)
{
    arena scratch = { 0 };
    script_effect_facts work = *facts;
    work.expressions = copy(
        &scratch,
        facts->expressions,
        (size_t)facts->expression_count * sizeof *facts->expressions
    );
    work.statements = copy(
        &scratch,
        facts->statements,
        (size_t)facts->statement_count * sizeof *facts->statements
    );
    work.functions = copy(
        &scratch,
        facts->functions,
        (size_t)facts->function_count * sizeof *facts->functions
    );
    script_diagnostic *diagnostics = NULL;
    finish_effects(&work, &diagnostics);
    unsigned *es = arena_alloc(storage, (size_t)work.expression_count * sizeof *es);
    unsigned *ss = arena_alloc(storage, (size_t)work.statement_count * sizeof *ss);
    unsigned *fs = arena_alloc(storage, (size_t)work.function_count * sizeof *fs);
    for (int i = 0; i < work.expression_count; i++) {
        es[i] = work.expressions[i].effects;
    }
    for (int i = 0; i < work.statement_count; i++) {
        ss[i] = work.statements[i].effects;
    }
    for (int i = 0; i < work.function_count; i++) {
        fs[i] = work.functions[i].effects;
    }
    int count = acount(diagnostics);
    script_diagnostic *ds = copy(storage, diagnostics, (size_t)count * sizeof *ds);
    afree(diagnostics);
    arena_free(&scratch);
    return (script_effect_result){ es, work.expression_count,
                                   ss, work.statement_count,
                                   fs, work.function_count,
                                   ds, count };
}
script_binding_order script_effect_order(arena *storage, const script_effect_facts *facts)
{
    int count = facts->binding_count;
    int *order = arena_alloc(storage, (size_t)count * sizeof *order);
    bool *emitted = xcalloc((size_t)count + 1, sizeof *emitted);
    for (int pass = 0; pass < count; pass++) {
        int found = -1;
        for (int i = 0; i < count; i++) {
            if (emitted[i]) {
                continue;
            }
            bool ready = true;
            for (int j = 0; j < count && ready; j++) {
                if (emitted[j]) {
                    continue;
                }
                const script_binding *r = &facts->bindings[i],
                                     *w = &facts->bindings[j];
                for (int x = 0; x < r->read_count; x++) {
                    for (int y = 0; y < w->write_count; y++) {
                        if (r->reads[x] == w->writes[y]) {
                            ready = false;
                        }
                    }
                }
            }
            if (ready) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            for (int i = 0; i < count; i++) {
                order[i] = i;
            }
            script_diagnostic *d = arena_alloc(storage, sizeof *d);
            *d = (script_diagnostic){ .range = facts->bindings[0].range };
            snprintf(d->message, sizeof d->message, "Binding dependency cycle");
            free(emitted);
            return (script_binding_order){ order, count, d, 1 };
        }
        emitted[found] = true;
        order[pass] = found;
    }
    free(emitted);
    return (script_binding_order){ order, count, NULL, 0 };
}
