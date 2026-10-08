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
void script_analyzer_finish_effects(analyzer *a)
{
    script_analysis *out = a->out;
    for (int pass = 0; pass <= acount(out->functions); pass++) {
        bool changed = false;
        for (int i = 0; i < acount(out->expressions); i++) {
            script_expression *expression = &out->expressions[i];
            unsigned effects = expression->effects;
            if (expression->left >= 0) {
                effects |= out->expressions[expression->left].effects;
            }
            if (expression->kind == SCRIPT_EXPR_CALL) {
                if (expression->right >= 0) {
                    effects |= out->functions[expression->right].effects;
                }
            } else if (expression->right >= 0) {
                effects |= out->expressions[expression->right].effects;
            }
            if (expression->kind == SCRIPT_EXPR_SYMBOL && expression->symbol >= 0 &&
                (out->symbols[expression->symbol].kind == SCRIPT_LAZY ||
                 out->symbols[expression->symbol].lazy_local) &&
                out->symbols[expression->symbol].initializer >= 0) {
                effects |= out->expressions[out->symbols[expression->symbol].initializer]
                               .effects;
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
    const unsigned impure = SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_STRUCTURAL |
                            SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND |
                            SCRIPT_EFFECT_RANDOM;
    for (int i = 0; i < acount(out->functions); i++) {
        script_function *function = &out->functions[i];
        if (function->pure && function->effects & impure) {
            script_diagnostic d = { .range = function->range };
            snprintf(d.message, sizeof d.message, "Pure function performs execution effects");
            apush(out->diagnostics, d);
        }
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
void script_analyzer_callable_definition(analyzer *a, int index, TSNode body)
{
    int old_scope = a->scope, old_function = a->function;
    a->function = index;
    a->scope = a->out->functions[index].scope;
    if (a->out->functions[index].kind != SCRIPT_FUNCTION_SYSTEM) {
        script_analyzer_declaration_pass(a, body);
        script_analyzer_definition_pass(a, body);
    }
    script_statement block = { .kind = SCRIPT_STMT_BLOCK,
                               .scope = a->scope,
                               .range = script_analyzer_range(body),
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
    a->scope = old_scope;
    a->function = old_function;
    asetlen(a->narrowed, 0);
}
void script_analyzer_host_declarations(analyzer *a, TSNode root)
{
    for (int i = 0; i < a->catalog->global_count; i++) {
        const script_catalog_global *global = &a->catalog->globals[i];
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
    for (int i = 0; i < a->catalog->capability_count; i++) {
        const script_catalog_capability *capability = &a->catalog->capabilities[i];
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
