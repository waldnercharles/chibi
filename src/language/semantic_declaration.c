#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool script_analyzer_token_present(const analyzer *a, TSNode node, const char *word)
{
    for (uint32_t i = 0; i < script_analyzer_child_count(node); i++) {
        if (script_analyzer_text_is(a, ts_node_child(node, i), word)) {
            return true;
        }
    }
    return false;
}
void script_analyzer_declare_value(analyzer *a, TSNode node)
{
    script_symbol_kind kind = script_analyzer_is(node, "action_declaration")
                                ? SCRIPT_ACTION
                            : script_analyzer_is(node, "event_declaration") ? SCRIPT_EVENT
                            : script_analyzer_text_is(a, script_analyzer_field(node, "storage"), "shared")
                                ? SCRIPT_SHARED_STATE
                                : SCRIPT_STATE;
    if (kind == SCRIPT_SHARED_STATE && a->out->kind != SCRIPT_PROGRAM_SYSTEM) {
        script_analyzer_diagnostic(a, node, "Shared state is restricted to systems");
    }
    script_type type = script_analyzer_type_node(a, script_analyzer_field(node, "type"));
    if (kind == SCRIPT_SHARED_STATE && (type.flags & SCRIPT_TYPE_TASK)) {
        /* A task belongs to the entity that started it. */
        script_analyzer_diagnostic(a, node, "Shared state cannot hold a task");
    }
    int symbol = script_analyzer_symbol_new(
        a,
        script_analyzer_field(node, "name"),
        kind,
        type,
        kind == SCRIPT_STATE || kind == SCRIPT_SHARED_STATE
    );
    a->out->symbols[symbol].disabled = script_analyzer_token_present(a, node, "disabled");
    apush(a->initializers, ((initializer){ symbol, script_analyzer_field(node, "value") }));
}
int script_analyzer_function_new(analyzer *a, TSNode node, script_function_kind kind, int symbol)
{
    script_function function = {
        .kind = kind,
        .symbol = symbol,
        .scope = -1,
        .body = -1,
        .receiver = -1,
        .parent = a->function,
        .range = script_analyzer_range(node),
        .pure = script_analyzer_token_present(a, node, "pure"),
        .depth_limit = a->function >= 0 ? a->out->functions[a->function].depth_limit : 64
    };
    apush(a->out->functions, function);
    return acount(a->out->functions) - 1;
}
void script_analyzer_parameters(analyzer *a, TSNode node, int function_index, bool results)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        TSNode parameter = ts_node_named_child(node, i);
        if (ts_node_is_extra(parameter)) {
            continue;
        }
        script_type type = script_analyzer_type_node(a, script_analyzer_field(parameter, "type"));
        if (type.flags & SCRIPT_TYPE_UNTYPED) {
            script_analyzer_diagnostic(a, parameter, "Callable parameters and results require a type");
            type = script_analyzer_bad_type();
        }
        int symbol = script_analyzer_symbol_new(
            a,
            script_analyzer_field(parameter, "name"),
            results ? SCRIPT_RESULT : SCRIPT_PARAMETER,
            type,
            true
        );
        script_function *function = &a->out->functions[function_index];
        if (results) {
            apush(function->results, symbol);
        } else {
            apush(function->parameters, symbol);
        }
        TSNode value = script_analyzer_field(parameter, "value");
        if (!ts_node_is_null(value)) {
            apush(a->initializers, ((initializer){ symbol, value }));
        }
    }
}
static bool returns_value(TSNode node)
{
    if (script_analyzer_is(node, "return_statement")) {
        return !ts_node_is_null(script_analyzer_field(node, "value"));
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!script_analyzer_is(child, "function_declaration") &&
            !script_analyzer_is(child, "macro_declaration") && returns_value(child)) {
            return true;
        }
    }
    return false;
}
/* A callable's value is its single result, a struct of several, or flow. */
void script_analyzer_function_type(analyzer *a, int function)
{
    int symbol = a->out->functions[function].symbol;
    int count = acount(a->out->functions[function].results);
    a->out->symbols[symbol]
        .type = count == 1
                  ? a->out->symbols[a->out->functions[function].results[0]].type
                  : (script_type){ .storage = count ? PIN_STRUCT : PIN_FLOW,
                                   .nominal = count ? a->out->symbols[symbol].id : 0,
                                   .flags = count ? SCRIPT_TYPE_RESULTS : 0 };
}
void script_analyzer_declare_function(analyzer *a, TSNode node)
{
    bool macro = script_analyzer_is(node, "macro_declaration");
    int symbol = script_analyzer_symbol_new(
        a,
        script_analyzer_field(node, "name"),
        macro ? SCRIPT_MACRO : SCRIPT_FUNCTION,
        (script_type){ .storage = PIN_FLOW },
        false
    );
    int function = script_analyzer_function_new(a, node, SCRIPT_FUNCTION_AUTHORED, symbol);
    a->out->symbols[symbol].function = function;
    a->out->symbols[symbol].capability = a->out->symbols[symbol].id;
    int old_scope = a->scope, old_function = a->function;
    a->function = function;
    a->scope = script_analyzer_scope_new(a, script_analyzer_field(node, "body"));
    a->out->functions[function].scope = a->scope;
    TSNode generics = script_analyzer_field(node, "type_parameters");
    for (uint32_t i = 0; i < script_analyzer_named_count(generics); i++) {
        TSNode parameter = ts_node_named_child(generics, i);
        if (ts_node_is_extra(parameter)) {
            continue;
        }
        script_type bound = script_analyzer_type_node(a, script_analyzer_field(parameter, "type"));
        int generic = script_analyzer_symbol_new(
            a,
            script_analyzer_field(parameter, "name"),
            SCRIPT_TYPE_PARAMETER,
            bound,
            false
        );
        a->out->symbols[generic].type.parameter = a->out->symbols[generic].id;
        a->out->symbols[generic].type.flags |= SCRIPT_TYPE_GENERIC;
        apush(a->out->functions[function].type_parameters, generic);
    }
    script_analyzer_parameters(a, script_analyzer_field(node, "parameters"), function, false);
    script_analyzer_parameters(a, script_analyzer_field(node, "results"), function, true);
    apush(
        a->out->symbols,
        ((script_symbol){
            .name = xstrdup("entry"),
            .id = script_catalog_id("entry", a->out->symbols[symbol].name),
            .kind = SCRIPT_PARAMETER,
            .type = { .storage = PIN_STRUCT,
                      .nominal = a->out->symbols[symbol].id,
                      .flags = SCRIPT_TYPE_RESULTS },
            .scope = a->scope,
            .initializer = -1,
            .function = function,
            .declaration = { .start = -1, .end = -1 } })
    );
    a->out->functions[function]
        .inferred = !macro &&
                    ts_node_is_null(script_analyzer_field(node, "results")) &&
                    returns_value(script_analyzer_field(node, "body"));
    script_analyzer_function_type(a, function);
    apush(a->bodies, ((function_body){ function, script_analyzer_field(node, "body"), BODY_PENDING }));
    TSNode body = script_analyzer_field(node, "body");
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode declaration = ts_node_named_child(body, i);
        if (script_analyzer_is(declaration, "entry_declaration")) {
            int entry = script_analyzer_function_new(a, declaration, SCRIPT_FUNCTION_MACRO_ENTRY, -1);
            a->out->functions[entry].scope = a->scope;
            a->out->functions[entry].entry = script_analyzer_name_copy(
                a,
                script_analyzer_field(declaration, "name")
            );
        } else if (script_analyzer_is(declaration, "exit_declaration")) {
            int outcome = script_analyzer_symbol_new(
                a,
                script_analyzer_field(declaration, "name"),
                SCRIPT_LABEL,
                (script_type){ .storage = PIN_FLOW },
                false
            );
            a->out->symbols[outcome].outcome = true;
        }
    }
    a->scope = old_scope;
    a->function = old_function;
}
int script_analyzer_query_expression(analyzer *a, TSNode node)
{
    script_query result = { .range = script_analyzer_range(node) };
    TSNode function = script_analyzer_field(node, "function"),
           arguments = script_analyzer_field(node, "arguments");
    if (!script_analyzer_is(node, "call_expression")) {
        script_analyzer_diagnostic(a, node, "Query requires has, all, any or not terms");
        result.kind = SCRIPT_QUERY_ALL;
    } else if (script_analyzer_text_is(a, function, "has")) {
        result.kind = SCRIPT_QUERY_HAS;
        TSNode value = ts_node_named_child(arguments, 0);
        char *name = script_analyzer_name_copy(a, value);
        int component = script_catalog_find_type(a->catalog, name);
        if (component < 0 || !a->catalog->types[component].component) {
            script_analyzer_diagnostic(a, value, "Unknown/ambiguous query component '%s'", name);
        } else {
            result.component = a->catalog->types[component].component_id;
        }
        if (script_analyzer_named_count(arguments) != 1) {
            script_analyzer_diagnostic(a, node, "has requires one component");
        }
        free(name);
    } else {
        if (script_analyzer_text_is(a, function, "all")) {
            result.kind = SCRIPT_QUERY_ALL;
        } else if (script_analyzer_text_is(a, function, "any")) {
            result.kind = SCRIPT_QUERY_ANY;
        } else if (script_analyzer_text_is(a, function, "not")) {
            result.kind = SCRIPT_QUERY_NOT;
        } else {
            script_analyzer_diagnostic(a, function, "Unknown query operator");
            result.kind = SCRIPT_QUERY_ALL;
        }
        for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
            TSNode term = ts_node_named_child(arguments, i);
            if (!ts_node_is_extra(term)) {
                apush(result.children, script_analyzer_query_expression(a, term));
            }
        }
        if (result.kind == SCRIPT_QUERY_NOT && acount(result.children) != 1) {
            script_analyzer_diagnostic(a, node, "not requires exactly one query term");
        }
    }
    result.child_count = acount(result.children);
    apush(a->out->queries, result);
    return acount(a->out->queries) - 1;
}
int script_analyzer_query_component(analyzer *a, TSNode node, uint64_t component, bool excluded)
{
    apush(
        a->out->queries,
        ((script_query){ .kind = SCRIPT_QUERY_HAS,
                         .component = component,
                         .range = script_analyzer_range(node) })
    );
    int index = acount(a->out->queries) - 1;
    if (!excluded) {
        return index;
    }
    script_query not = { .kind = SCRIPT_QUERY_NOT,
                         .range = script_analyzer_range(node) };
    apush(not.children, index);
    not.child_count = 1;
    apush(a->out->queries, not);
    return acount(a->out->queries) - 1;
}
void script_analyzer_query_intersect(analyzer *a, int term)
{
    if (a->out->query < 0) {
        a->out->query = term;
        return;
    }
    script_query all = { .kind = SCRIPT_QUERY_ALL };
    apush(all.children, a->out->query);
    apush(all.children, term);
    all.child_count = 2;
    apush(a->out->queries, all);
    a->out->query = acount(a->out->queries) - 1;
}
void script_analyzer_system_parameters(analyzer *a, TSNode root)
{
    TSNode parameters_node = script_analyzer_field(root, "parameters");
    for (uint32_t i = 0; i < script_analyzer_named_count(parameters_node); i++) {
        TSNode parameter = ts_node_named_child(parameters_node, i);
        if (ts_node_is_extra(parameter)) {
            continue;
        }
        script_type type = script_analyzer_type_node(a, script_analyzer_field(parameter, "type"));
        const script_catalog_type *component = script_catalog_type_by_id(a->catalog, type.nominal);
        if (!component || !component->component) {
            script_analyzer_diagnostic(a, parameter, "System parameter requires a reflected component type");
        }
        script_analyzer_symbol_new(
            a,
            script_analyzer_field(parameter, "name"),
            SCRIPT_COMPONENT,
            type,
            !component || !component->derived
        );
        if (component && !(type.flags & SCRIPT_TYPE_OPTIONAL)) {
            script_analyzer_query_intersect(
                a,
                script_analyzer_query_component(a, parameter, component->component_id, false)
            );
        }
    }
    TSNode excluded = script_analyzer_field(root, "without");
    for (uint32_t i = 0; i < script_analyzer_named_count(excluded); i++) {
        TSNode node = ts_node_named_child(excluded, i);
        if (!script_analyzer_is(node, "type")) {
            continue;
        }
        script_type type = script_analyzer_type_node(a, node);
        const script_catalog_type *component = script_catalog_type_by_id(a->catalog, type.nominal);
        if (!component || !component->component) {
            script_analyzer_diagnostic(a, node, "Excluded type must be a reflected component");
            continue;
        }
        for (int j = 0; j < acount(a->out->symbols); j++) {
            if (a->out->symbols[j].kind == SCRIPT_COMPONENT &&
                a->out->symbols[j].type.nominal == component->id) {
                script_analyzer_diagnostic(a, node, "A component cannot be both a parameter and excluded");
            }
        }
        script_analyzer_query_intersect(
            a,
            script_analyzer_query_component(a, node, component->component_id, true)
        );
    }
}
void script_analyzer_declaration_pass(analyzer *a, TSNode body)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (script_analyzer_is(node, "state_declaration") ||
            script_analyzer_is(node, "action_declaration") ||
            script_analyzer_is(node, "event_declaration")) {
            script_analyzer_declare_value(a, node);
        } else if (script_analyzer_is(node, "function_declaration") || script_analyzer_is(node, "macro_declaration")) {
            script_analyzer_declare_function(a, node);
        } else if (script_analyzer_is(node, "local_declaration")) {
            int symbol = script_analyzer_symbol_new(
                a,
                script_analyzer_field(node, "name"),
                SCRIPT_LOCAL,
                script_analyzer_type_node(a, script_analyzer_field(node, "type")),
                true
            );
            apush(a->initializers, ((initializer){ symbol, script_analyzer_field(node, "value") }));
        } else if (script_analyzer_is(node, "handler") && script_analyzer_text_is(a, script_analyzer_field(node, "kind"), "on")) {
            TSNode alias = script_analyzer_field(node, "alias");
            if (ts_node_is_null(alias)) {
                continue;
            }
            char *name = script_analyzer_qualified_name(a, script_analyzer_field(node, "name"));
            int event = script_catalog_find_capability(a->catalog, name, SCRIPT_CAPABILITY_EVENT);
            free(name);
            if (event < 0) {
                continue;
            }
            uint64_t id = a->catalog->capabilities[event].id;
            int symbol = script_analyzer_symbol_new(
                a,
                alias,
                SCRIPT_PARAMETER,
                (script_type){ .storage = PIN_STRUCT, .nominal = id, .flags = SCRIPT_TYPE_RESULTS },
                false
            );
            a->out->symbols[symbol].capability = id;
        } else if (script_analyzer_is(node, "exit_declaration")) {
            script_range range = script_analyzer_range(script_analyzer_field(node, "name"));
            bool declared = false;
            for (int j = 0; j < acount(a->out->symbols); j++) {
                if (a->out->symbols[j].kind == SCRIPT_LABEL &&
                    a->out->symbols[j].scope == a->scope &&
                    a->out->symbols[j].declaration.start == range.start) {
                    declared = true;
                    break;
                }
            }
            if (declared) {
                continue;
            }
            int outcome = script_analyzer_symbol_new(
                a,
                script_analyzer_field(node, "name"),
                SCRIPT_LABEL,
                (script_type){ .storage = PIN_FLOW },
                false
            );
            a->out->symbols[outcome].outcome = true;
        } else if (script_analyzer_is(node, "events_declaration")) {
            int old = a->scope;
            a->scope = script_analyzer_scope_new(a, script_analyzer_field(node, "body"));
            script_analyzer_declaration_pass(a, script_analyzer_field(node, "body"));
            a->scope = old;
        } else if (
            script_analyzer_is(node, "metadata_declaration") &&
            script_analyzer_text_is(a, script_analyzer_field(node, "kind"), "depth_limit")
        ) {
            TSNode value = script_analyzer_field(node, "value");
            char *text = script_analyzer_text_copy(a, value), *end;
            errno = 0;
            long limit = strtol(text, &end, 10);
            if (*end || errno == ERANGE || limit < 0 || limit > INT_MAX ||
                a->function < 0) {
                script_analyzer_diagnostic(a, node, "Callable depth limit must be a nonnegative integer");
            } else {
                a->out->functions[a->function].depth_limit = (int)limit;
            }
            free(text);
        } else if (
            script_analyzer_is(node, "metadata_declaration") &&
            script_analyzer_text_is(a, script_analyzer_field(node, "kind"), "owner")
        ) {
            script_type owner = script_analyzer_type_node(a, script_analyzer_field(node, "value"));
            if (owner.storage != PIN_HANDLE) {
                script_analyzer_diagnostic(a, node, "Owner must be a nominal handle type");
            }
            a->out->owner_type = owner.nominal;
        }
    }
}
int script_analyzer_scope_at(const analyzer *a, TSNode body)
{
    script_range range = script_analyzer_range(body);
    for (int i = 0; i < acount(a->out->scopes); i++) {
        if (a->out->scopes[i].range.start == range.start &&
            a->out->scopes[i].range.end == range.end) {
            return i;
        }
    }
    return a->scope;
}
static script_catalog_parameter *authored_payload(const analyzer *a, const int *symbols)
{
    script_catalog_parameter *payload = NULL;
    for (int i = 0; i < acount(symbols); i++) {
        const script_symbol *symbol = &a->out->symbols[symbols[i]];
        apush(
            payload,
            ((script_catalog_parameter){ .name = symbol->name, .type = symbol->type })
        );
    }
    return payload;
}
void script_analyzer_handler(analyzer *a, TSNode node)
{
    TSNode name_node = script_analyzer_field(node, "name"),
           body = script_analyzer_field(node, "body");
    char *name = script_analyzer_qualified_name(a, name_node);
    bool after = script_analyzer_text_is(a, script_analyzer_field(node, "kind"), "after");
    const script_catalog_capability *signature = NULL;
    int event_index = script_catalog_find_capability(a->catalog, name, SCRIPT_CAPABILITY_EVENT);
    int event_alias = script_analyzer_lookup(a, name_node);
    bool declared_event = event_alias >= 0 &&
                          a->out->symbols[event_alias].kind == SCRIPT_EVENT &&
                          a->out->symbols[event_alias].declaration.start >= 0;
    int action = -1;
    int receiver = -1;
    char *outcome = NULL;
    script_catalog_capability authored = { 0 };
    script_catalog_parameter *authored_parameters = NULL, *authored_results = NULL;
    const char **authored_outcomes = NULL;
    const char *completion[] = { "Then" };
    bool entry_handler = after && !strcmp(name, "entry") && a->function >= 0;
    if (entry_handler) {
        const script_function *parent = &a->out->functions[a->function];
        authored_results = authored_payload(a, parent->parameters);
        authored = (script_catalog_capability){
            .id = parent->symbol >= 0 ? a->out->symbols[parent->symbol].id : 0,
            .results = authored_results,
            .result_count = acount(authored_results),
            .outcomes = completion,
            .outcome_count = 1,
            .completion_outcome = 0
        };
        signature = &authored;
        action = parent->symbol;
        outcome = xstrdup("entry");
    } else if (after) {
        if (script_analyzer_is(name_node, "member_expression")) {
            action = script_analyzer_lookup(a, script_analyzer_field(name_node, "object"));
            outcome = script_analyzer_name_copy(a, script_analyzer_field(name_node, "member"));
        } else {
            action = script_analyzer_lookup(a, name_node);
        }
        if (action < 0 || a->out->symbols[action].kind != SCRIPT_ACTION) {
            script_analyzer_diagnostic(a, name_node, "After requires a declared action");
        } else {
            script_analyzer_resolve_initializer(a, action);
            signature = script_catalog_capability_by_id(
                a->catalog,
                a->out->symbols[action].capability
            );
            int initializer = a->out->symbols[action].initializer;
            if (signature && initializer >= 0 &&
                a->out->expressions[initializer].result_count) {
                const script_expression *configuration = &a->out->expressions[initializer];
                for (int i = 0; i < signature->result_count; i++) {
                    script_catalog_parameter parameter = signature->results[i];
                    if (i < configuration->result_count) {
                        parameter.type = configuration->results[i];
                    }
                    apush(authored_results, parameter);
                }
                authored = *signature;
                authored.results = authored_results;
                signature = &authored;
            }
            int authored_function = initializer >= 0 &&
                                            a->out->expressions[initializer].kind == SCRIPT_EXPR_CALL
                                      ? a->out->expressions[initializer].right
                                      : -1;
            if (!signature && authored_function >= 0) {
                const script_function *parent = &a->out->functions[authored_function];
                authored_parameters = authored_payload(a, parent->parameters);
                authored_results = authored_payload(a, parent->results);
                const char **outcomes = NULL;
                for (int i = 0; i < acount(a->out->symbols); i++) {
                    const script_symbol *symbol = &a->out->symbols[i];
                    if (symbol->outcome && symbol->scope == parent->scope) {
                        apush(outcomes, symbol->name);
                    }
                }
                authored = (script_catalog_capability){
                    .id = a->out->symbols[action].capability,
                    .parameters = authored_parameters,
                    .parameter_count = acount(authored_parameters),
                    .results = authored_results,
                    .result_count = acount(authored_results),
                    .outcomes = acount(outcomes) ? outcomes : completion,
                    .outcome_count = acount(outcomes) ? acount(outcomes) : 1,
                    .completion_outcome = acount(outcomes) ? -1 : 0
                };
                signature = &authored;
                authored_outcomes = outcomes;
            }
            bool found = !outcome && signature && signature->outcome_count == 1;
            for (int i = 0; outcome && signature && i < signature->outcome_count; i++) {
                found |= script_name_matches(signature->outcomes[i], outcome);
            }
            if (!found) {
                script_analyzer_diagnostic(a, name_node, "Unknown or ambiguous action outcome");
            }
        }
    } else if (event_index >= 0 && !declared_event) {
        signature = &a->catalog->capabilities[event_index];
        script_analyzer_use(a, name_node, -1, SCRIPT_HANDLE, signature->id);
    } else if (event_alias >= 0 && a->out->symbols[event_alias].kind == SCRIPT_EVENT) {
        int alias = event_alias;
        script_analyzer_resolve_initializer(a, alias);
        signature = script_catalog_capability_by_id(
            a->catalog,
            a->out->symbols[alias].capability
        );
        if (signature && signature->kind != SCRIPT_CAPABILITY_EVENT) {
            signature = NULL;
        }
        script_analyzer_use(
            a,
            name_node,
            alias,
            SCRIPT_HANDLE,
            signature ? signature->id : 0
        );
    } else if (script_analyzer_is(name_node, "member_expression")) {
        receiver = script_analyzer_expression(
            a,
            script_analyzer_field(name_node, "object"),
            (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
        );
        char *event = script_analyzer_name_copy(a, script_analyzer_field(name_node, "member"));
        uint64_t type = script_analyzer_expr_type(a, receiver).nominal;
        for (int i = 0; i < a->catalog->capability_count; i++) {
            const script_catalog_capability *candidate = &a->catalog->capabilities[i];
            if (candidate->kind == SCRIPT_CAPABILITY_EVENT &&
                candidate->receiver == type &&
                script_name_matches(candidate->name, event)) {
                if (signature) {
                    script_analyzer_diagnostic(a, name_node, "Ambiguous component event");
                    signature = NULL;
                    break;
                }
                signature = candidate;
            }
        }
        free(event);
    }
    if (!signature) {
        script_analyzer_diagnostic(a, name_node, "%s event '%s'", event_index == -2 ? "Ambiguous" : "Unknown", name);
    }
    int function = script_analyzer_function_new(
        a,
        node,
        entry_handler ? SCRIPT_FUNCTION_MACRO_ENTRY
        : after       ? SCRIPT_FUNCTION_CONTINUATION
                      : SCRIPT_FUNCTION_HANDLER,
        -1
    );
    a->out->functions[function].event = signature && !after ? signature->id : 0;
    a->out->functions[function].receiver = receiver;
    a->out->functions[function].action = action >= 0 ? a->out->symbols[action].id : 0;
    a->out->functions[function].entry = outcome;
    if (after && !outcome && signature && signature->completion_outcome >= 0) {
        if (signature->completion_outcome >= signature->outcome_count ||
            !signature->outcomes) {
            script_analyzer_diagnostic(a, name_node, "Catalog completion outcome is outside declared outcomes");
        } else {
            a->out->functions[function].entry = xstrdup(
                signature->outcomes[signature->completion_outcome]
            );
        }
    }
    TSNode alias_node = script_analyzer_field(node, "alias");
    int declared_alias = !after && !ts_node_is_null(alias_node)
                           ? script_analyzer_lookup(a, alias_node)
                           : -1;
    if (declared_alias >= 0) {
        a->out->symbols[declared_alias].function = function;
    }
    int old_scope = a->scope, old_function = a->function;
    a->function = function;
    a->scope = script_analyzer_scope_new(a, body);
    a->out->functions[function].scope = a->scope;
    TSNode parameter_nodes = script_analyzer_field(node, "parameters");
    int ordinal = 0;
    const script_catalog_parameter *payload = signature ? after ? signature->results
                                                                : signature->parameters
                                                        : NULL;
    int count = signature ? after ? signature->result_count : signature->parameter_count
                          : 0;
    for (uint32_t i = 0; i < script_analyzer_named_count(parameter_nodes); i++) {
        TSNode parameter = ts_node_named_child(parameter_nodes, i);
        if (ts_node_is_extra(parameter)) {
            continue;
        }
        TSNode parameter_name = script_analyzer_is(parameter, "parameter")
                                  ? script_analyzer_field(parameter, "name")
                                  : parameter;
        script_type type = ordinal < count ? payload[ordinal].type
                                           : script_analyzer_bad_type();
        TSNode annotation = script_analyzer_field(parameter, "type");
        if (!ts_node_is_null(annotation)) {
            script_analyzer_require(a, annotation, type, script_analyzer_type_node(a, annotation));
        }
        int symbol = script_analyzer_symbol_new(
            a,
            parameter_name,
            SCRIPT_PARAMETER,
            type,
            (type.flags & SCRIPT_TYPE_REFERENCE) != 0
        );
        apush(a->out->functions[function].parameters, symbol);
        ordinal++;
    }
    if (!script_analyzer_named_count(parameter_nodes)) {
        for (int i = 0; i < count; i++) {
            script_symbol hidden = {
                .name = xstrdup(payload[i].name),
                .kind = SCRIPT_PARAMETER,
                .id = script_catalog_id("payload-port", payload[i].name),
                .type = payload[i].type,
                .scope = a->scope,
                .initializer = -1,
                .function = function,
                .declaration = { .start = -1, .end = -1 },
                .writable = (payload[i].type.flags & SCRIPT_TYPE_REFERENCE) != 0
            };
            apush(a->out->symbols, hidden);
            apush(a->out->functions[function].parameters, acount(a->out->symbols) - 1);
        }
    }
    if (script_analyzer_named_count(parameter_nodes) && ordinal != count) {
        script_analyzer_diagnostic(a, parameter_nodes, "Expected %d payload parameters, got %d", count, ordinal);
    }
    /* The payload aggregate has a semantic identity, independent of aliases. */
    script_symbol event = { .name = xstrdup("event"),
                            .kind = SCRIPT_PARAMETER,
                            .id = script_catalog_id("payload", name),
                            .capability = signature ? signature->id : 0,
                            .type = { .storage = PIN_STRUCT,
                                      .nominal = signature ? signature->id : 0,
                                      .flags = SCRIPT_TYPE_RESULTS },
                            .scope = a->scope,
                            .initializer = -1,
                            .function = function,
                            .declaration = { .start = -1, .end = -1 } };
    apush(a->out->symbols, event);
    TSNode alias = script_analyzer_field(node, "alias");
    if (!ts_node_is_null(alias) && declared_alias < 0) {
        int symbol = script_analyzer_symbol_new(
            a,
            alias,
            SCRIPT_PARAMETER,
            event.type,
            false
        );
        a->out->symbols[symbol].capability = event.capability;
        a->out->symbols[symbol].function = function;
    }
    a->out->functions[function].body = script_analyzer_statement(a, body);
    a->out->functions[function]
        .effects = a->out->statements[a->out->functions[function].body].effects;
    a->scope = old_scope;
    a->function = old_function;
    asetlen(a->narrowed, 0);
    free(name);
    afree(authored_parameters);
    afree(authored_results);
    afree(authored_outcomes);
}
uint64_t script_analyzer_expression_path(const script_analysis *out, int index)
{
    const script_expression *expression = &out->expressions[index];
    if (expression->kind == SCRIPT_EXPR_SYMBOL && expression->symbol >= 0) {
        return out->symbols[expression->symbol].id;
    }
    uint64_t parent = expression->left >= 0
                        ? script_analyzer_expression_path(out, expression->left)
                        : 0;
    if (expression->kind == SCRIPT_EXPR_MEMBER ||
        expression->kind == SCRIPT_EXPR_COMPONENT) {
        return (parent ^ expression->member) * UINT64_C(1099511628211);
    }
    return 0;
}
void script_analyzer_binding_reads(analyzer *a, script_binding *binding, int index, int depth)
{
    if (index < 0 || depth >= SCRIPT_MAX_DEPTH) {
        return;
    }
    const script_expression *expression = &a->out->expressions[index];
    if (expression->kind == SCRIPT_EXPR_MEMBER) {
        uint64_t path = script_analyzer_expression_path(a->out, index);
        bool exists = false;
        for (int i = 0; i < acount(binding->reads); i++) {
            exists |= binding->reads[i] == path;
        }
        if (path && !exists) {
            apush(binding->reads, path);
        }
    }
    script_analyzer_binding_reads(a, binding, expression->left, depth + 1);
    if (expression->kind != SCRIPT_EXPR_CALL) {
        script_analyzer_binding_reads(a, binding, expression->right, depth + 1);
    }
    for (int i = 0; i < acount(expression->arguments); i++) {
        script_analyzer_binding_reads(a, binding, expression->arguments[i].value, depth + 1);
    }
}
void script_analyzer_binding(analyzer *a, TSNode node)
{
    if (a->out->kind == SCRIPT_PROGRAM_LIBRARY || a->function >= 0) {
        script_analyzer_diagnostic(a, node, "Bindings belong to behavior or system roots");
    }
    script_binding result = { .range = script_analyzer_range(node) };
    result.target = script_analyzer_expression(
        a,
        script_analyzer_field(node, "target"),
        (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
    );
    script_expression target = a->out->expressions[result.target];
    if (!target.writable || target.kind != SCRIPT_EXPR_MEMBER) {
        script_analyzer_diagnostic(a, script_analyzer_field(node, "target"), "Binding requires a writable reflected component field");
    }
    result.value = script_analyzer_expression(
        a,
        script_analyzer_field(node, "value"),
        target.type
    );
    script_analyzer_require(
        a,
        node,
        target.type,
        script_analyzer_expr_type(a, result.value)
    );
    apush(result.writes, script_analyzer_expression_path(a->out, result.target));
    script_analyzer_binding_reads(a, &result, result.value, 0);
    result.read_count = acount(result.reads);
    result.write_count = acount(result.writes);
    for (int i = 0; i < acount(a->out->bindings); i++) {
        if (a->out->bindings[i].writes[0] == result.writes[0]) {
            script_analyzer_diagnostic(a, node, "Multiple bindings write the same destination");
        }
    }
    apush(a->out->bindings, result);
}
bool script_analyzer_declaration_kind(TSNode node)
{
    return script_analyzer_is(node, "state_declaration") ||
           script_analyzer_is(node, "action_declaration") ||
           script_analyzer_is(node, "event_declaration") ||
           script_analyzer_is(node, "function_declaration") ||
           script_analyzer_is(node, "macro_declaration") ||
           script_analyzer_is(node, "handler") ||
           script_analyzer_is(node, "events_declaration") ||
           script_analyzer_is(node, "metadata_declaration") ||
           script_analyzer_is(node, "query_declaration") ||
           script_analyzer_is(node, "bind_declaration") ||
           script_analyzer_is(node, "entry_declaration") ||
           script_analyzer_is(node, "exit_declaration");
}
void script_analyzer_definition_pass(analyzer *a, TSNode body)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (ts_node_is_extra(node)) {
            continue;
        }
        if (script_analyzer_is(node, "handler")) {
            script_analyzer_handler(a, node);
        } else if (script_analyzer_is(node, "bind_declaration")) {
            script_analyzer_binding(a, node);
        } else if (script_analyzer_is(node, "query_declaration")) {
            if (a->out->kind != SCRIPT_PROGRAM_SYSTEM) {
                script_analyzer_diagnostic(a, node, "Queries belong to system declarations");
            }
            script_analyzer_query_intersect(
                a,
                script_analyzer_query_expression(a, script_analyzer_field(node, "value"))
            );
        } else if (script_analyzer_is(node, "events_declaration")) {
            int old = a->scope;
            a->scope = script_analyzer_scope_at(a, script_analyzer_field(node, "body"));
            script_analyzer_definition_pass(a, script_analyzer_field(node, "body"));
            a->scope = old;
        } else if (script_analyzer_is(node, "entry_declaration")) {
            script_range range = script_analyzer_range(node);
            int function = -1;
            for (int j = 0; j < acount(a->out->functions); j++) {
                if (a->out->functions[j].kind == SCRIPT_FUNCTION_MACRO_ENTRY &&
                    a->out->functions[j].parent == a->function &&
                    a->out->functions[j].range.start == range.start) {
                    function = j;
                    break;
                }
            }
            if (function < 0) {
                function = script_analyzer_function_new(a, node, SCRIPT_FUNCTION_MACRO_ENTRY, -1);
                a->out->functions[function].entry = script_analyzer_name_copy(
                    a,
                    script_analyzer_field(node, "name")
                );
            }
            int old_function = a->function;
            a->function = function;
            a->out->functions[function].scope = a->scope;
            a->out->functions[function]
                .body = script_analyzer_statement(a, script_analyzer_field(node, "body"));
            a->out->functions[function]
                .effects = a->out->statements[a->out->functions[function].body].effects;
            a->function = old_function;
        } else if (!script_analyzer_declaration_kind(node) && !script_analyzer_is(body, "system_body") && !script_analyzer_is(body, "callable_body")) {
            script_analyzer_diagnostic(a, node, "Unsupported root declaration '%s'", ts_node_type(node));
        }
    }
}
