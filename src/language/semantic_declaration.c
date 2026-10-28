#include "language/callable_headers.h"
#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
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
void script_analyzer_declare_constant(analyzer *a, TSNode node)
{
    if (script_analyzer_generic_alias(a, node)) {
        return;
    }
    TSNode type = script_analyzer_field(node, "type");
    int symbol = script_analyzer_symbol_new(
        a,
        script_analyzer_field(node, "name"),
        SCRIPT_CONSTANT,
        ts_node_is_null(type) ? (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
                              : script_analyzer_type_node(a, type),
        false
    );
    apush(a->initializers, ((initializer){ symbol, script_analyzer_field(node, "value") }));
}
void script_analyzer_declare_value(analyzer *a, TSNode node)
{
    script_symbol_kind kind = script_analyzer_is(node, "action_declaration")
                                ? SCRIPT_ACTION
                            : script_analyzer_is(node, "event_declaration") ? SCRIPT_EVENT
                            : a->out->kind == SCRIPT_PROGRAM_SYSTEM
                                ? SCRIPT_SHARED_STATE
                                : SCRIPT_STATE;
    script_type type = script_analyzer_type_node(a, script_analyzer_field(node, "type"));
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
static script_callable_facts callable_facts(const analyzer *a, const script_signature_generic *generics)
{
    return (script_callable_facts){
        .signature = { .named = script_analyzer_named_facts(a),
                       .functions = a->out->functions,
                       .function_count = acount(a->out->functions),
                       .pointer_size = sizeof(void *),
                       .generics = generics,
                       .generic_count = acount(generics) },
        .scope_count = acount(a->out->scopes)
    };
}
static int *callable_members(const int *members, int count)
{
    int *owned = NULL;
    for (int i = 0; i < count; i++) {
        apush(owned, members[i]);
    }
    return owned;
}
static void callable_symbol(analyzer *a, const script_callable_symbol_change *change)
{
    script_symbol row = change->symbol;
    unsigned fields = change->fields;
    if (fields & SCRIPT_CALLABLE_APPEND) {
        assert(change->index == acount(a->out->symbols));
        row.name = xstrdup(row.name);
        row.unit = row.unit ? xstrdup(row.unit) : NULL;
        apush(a->out->symbols, row);
        return;
    }
    script_symbol *symbol = &a->out->symbols[change->index];
    if (fields & SCRIPT_CALLABLE_TYPE) {
        symbol->type = row.type;
    }
    if (fields & SCRIPT_CALLABLE_UNIT) {
        free(symbol->unit);
        symbol->unit = row.unit ? xstrdup(row.unit) : NULL;
    }
    if (fields & SCRIPT_CALLABLE_RANGE) {
        symbol->declaration = row.declaration;
    }
    if (fields & SCRIPT_CALLABLE_FUNCTION) {
        symbol->function = row.function;
    }
    if (fields & SCRIPT_CALLABLE_CAPABILITY) {
        symbol->capability = row.capability;
    }
    if (fields & SCRIPT_CALLABLE_OUTCOME) {
        symbol->outcome = row.outcome;
    }
}
static void callable_function(analyzer *a, const script_callable_function_change *change)
{
    script_function row = change->function;
    row.parameters = callable_members(row.parameters, row.parameter_count);
    row.results = callable_members(row.results, row.result_count);
    row.entry = row.entry ? xstrdup(row.entry) : NULL;
    /* Counts in helper output describe its flat transport spans. The analyzer
     * continues to derive its finalized counts in finish_analysis. */
    row.parameter_count = row.result_count = 0;
    if (change->append) {
        assert(change->index == acount(a->out->functions));
        apush(a->out->functions, row);
        return;
    }
    script_function *function = &a->out->functions[change->index];
    row.parameter_count = function->parameter_count;
    row.result_count = function->result_count;
    afree(function->parameters);
    afree(function->results);
    free(function->entry);
    *function = row;
}
static void callable_apply(analyzer *a, script_callable_header header)
{
    for (int i = 0; i < header.symbol_count; i++) {
        callable_symbol(a, &header.symbols[i]);
    }
    for (int i = 0; i < header.scope_count; i++) {
        apush(a->out->scopes, header.scopes[i]);
    }
    for (int i = 0; i < header.function_count; i++) {
        callable_function(a, &header.functions[i]);
    }
    for (int i = 0; i < header.type_count; i++) {
        script_analyzer_publish_type(a, header.types[i]);
    }
    for (int i = 0; i < header.diagnostic_count; i++) {
        apush(a->out->diagnostics, header.diagnostics[i]);
    }
    for (int i = 0; i < header.use_count; i++) {
        apush(a->out->uses, header.uses[i]);
    }
    for (int i = 0; i < header.initializer_count; i++) {
        const script_callable_initializer *pending = &header.initializers[i];
        apush(a->initializers, ((initializer){ pending->symbol, pending->node }));
    }
    for (int i = 0; i < header.body_count; i++) {
        const script_callable_body *pending = &header.bodies[i];
        apush(a->bodies, ((function_body){ pending->function, pending->node, BODY_PENDING }));
    }
}
int script_analyzer_function_new(analyzer *a, TSNode node, script_function_kind kind, int symbol)
{
    script_function function = script_callable_function(
        a->function,
        node,
        script_analyzer_range(a, node).document,
        kind,
        symbol
    );
    apush(a->out->functions, function);
    return acount(a->out->functions) - 1;
}
void script_analyzer_function_type(analyzer *a, int function)
{
    const script_function *callable = &a->out->functions[function];
    script_type *results = NULL;
    for (int i = 0; i < acount(callable->results); i++) {
        apush(results, a->out->symbols[callable->results[i]].type);
    }
    a->out->symbols[callable->symbol].type = script_callable_result_type(
        a->out->symbols[callable->symbol].id,
        results,
        acount(results)
    );
    afree(results);
}
/* Whether `@test` marks a function declaration. It is the only attribute. */
static bool test_attribute(analyzer *a, TSNode node)
{
    bool test = false;
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        TSNode attribute = ts_node_named_child(node, i);
        if (!script_analyzer_is(attribute, "attribute")) {
            continue;
        }
        TSNode name = script_analyzer_field(attribute, "name");
        if (!script_analyzer_text_is(a, name, "test")) {
            char *text = script_analyzer_text_copy(a, name);
            script_analyzer_diagnostic(a, attribute, "Unknown attribute '@%s'", text);
            free(text);
        } else if (test) {
            script_analyzer_diagnostic(a, attribute, "Duplicate attribute '@test'");
        }
        test |= script_analyzer_text_is(a, name, "test");
    }
    return test;
}
/* A test is a package function with no parameters or results, so the
 * runner can call it alone. Its declared header says so,
 * including a result its returns imply. */
static void mark_test(analyzer *a, TSNode node, int function)
{
    if (a->function >= 0 || !script_analyzer_is(ts_node_parent(node), "source_file")) {
        script_analyzer_diagnostic(a, node, "@test needs a package-level function");
        return;
    }
    script_function *declared = &a->out->functions[function];
    if (acount(declared->parameters) || acount(declared->results) || declared->inferred) {
        script_analyzer_diagnostic(a, node, "A @test function takes no parameters and returns nothing");
        return;
    }
    declared->test = true;
}
void script_analyzer_declare_function(analyzer *a, TSNode node)
{
    bool test = !a->instantiating && test_attribute(a, node);
    if (!a->instantiating && script_analyzer_generic_declare(a, node)) {
        if (test) {
            script_analyzer_diagnostic(a, node, "A @test function takes no parameters and returns nothing");
        }
        return;
    }
    script_signature_generic *generics = NULL;
    script_analyzer_generic_types(a, script_analyzer_field(node, "parameters"), &generics);
    script_analyzer_generic_types(a, script_analyzer_field(node, "results"), &generics);
    script_callable_facts facts = callable_facts(a, generics);
    script_callable_header header = script_callable_register(
        &a->out->constants,
        &facts,
        node,
        script_analyzer_syntax(a, node)->text,
        (script_callable_context){ .scope = a->scope,
                                   .function = a->function,
                                   .document = script_analyzer_range(a, node).document,
                                   .specialization = a->specialization,
                                   .instantiating = a->instantiating }
    );
    afree(generics);
    callable_apply(a, header);
    if (!a->instantiating && header.function >= 0) {
        script_analyzer_generic_export(a, node, header.symbol);
    }
    /* A name the package already declares has no function of its own; the
     * duplicate is diagnosed where it is registered. */
    if (test && header.function >= 0) {
        mark_test(a, node, header.function);
    }
}
int script_analyzer_lambda(analyzer *a, TSNode node)
{
    script_range range = script_analyzer_range(a, node);
    for (int i = 0; i < acount(a->out->functions); i++) {
        const script_function *existing = &a->out->functions[i];
        if (existing->range.document == range.document &&
            existing->range.start == range.start && existing->scope >= 0 &&
            a->out->scopes[existing->scope].parent == a->scope) {
            return i;
        }
    }
    int function = acount(a->out->functions);
    script_analyzer_declare_function(a, node);
    if (function == acount(a->out->functions)) {
        return -1;
    }
    int body = acount(a->bodies) - 1;
    a->bodies[body].state = BODY_ACTIVE;
    int old_loops = a->loops;
    narrowing *old_narrowed = a->narrowed;
    TSNode old_awaited = a->awaited;
    a->loops = 0;
    a->narrowed = NULL;
    a->awaited = (TSNode){ 0 };
    script_analyzer_callable_definition(a, function, script_analyzer_field(node, "body"));
    script_analyzer_function_type(a, function);
    a->bodies[body].state = BODY_DONE;
    afree(a->narrowed);
    a->narrowed = old_narrowed;
    a->awaited = old_awaited;
    a->loops = old_loops;
    return function;
}
static uint64_t query_relationship_id(const analyzer *a, const char *name)
{
    if (!strcmp(name, "ChildOf")) {
        return SCRIPT_CHILD_OF;
    }
    for (int i = 0; i < a->catalog->relationship_count; i++) {
        if (!strcmp(a->catalog->relationships[i].name, name)) {
            return a->catalog->relationships[i].id;
        }
    }
    return 0;
}
static uint64_t query_relationship(analyzer *a, TSNode node, const char *name, bool ancestor)
{
    if (!strcmp(name, "ChildOf")) {
        return SCRIPT_CHILD_OF;
    }
    for (int i = 0; i < a->catalog->relationship_count; i++) {
        const script_catalog_relationship *r = &a->catalog->relationships[i];
        if (strcmp(r->name, name)) {
            continue;
        }
        if (!r->exclusive || (ancestor && !r->traversable)) {
            script_analyzer_diagnostic(a, node, ancestor ? "Ancestor source requires an exclusive traversable relationship" : "Scalar query path requires an exclusive relationship; iterate multiple targets explicitly");
        }
        return r->id;
    }
    script_analyzer_diagnostic(a, node, "Unknown query relationship '%s'", name);
    return 0;
}
static TSNode query_argument(TSNode arguments)
{
    TSNode result = { 0 };
    for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
        TSNode child = ts_node_named_child(arguments, i);
        if (ts_node_is_extra(child)) {
            continue;
        }
        if (!ts_node_is_null(result)) {
            return (TSNode){ 0 };
        }
        result = child;
    }
    return result;
}
/* A provider is the root, a previously bound row variable, or a scalar path. */
static int query_variable(analyzer *a, TSNode node)
{
    if (script_analyzer_text_is(a, node, "self")) {
        script_analyzer_use(a, node, a->out->owner_symbol, SCRIPT_READ, 0);
        return 0;
    }
    int symbol = script_analyzer_lookup(a, node);
    if (symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_QUERY_VARIABLE &&
        a->out->symbols[symbol].query_description == a->query_description) {
        script_analyzer_use(a, node, symbol, SCRIPT_READ, 0);
        return a->out->symbols[symbol].source_variable;
    }
    script_analyzer_diagnostic(a, node, "Query source requires self or an earlier query variable");
    return 0;
}
static uint64_t query_source(analyzer *a, TSNode source, int *traversal, int *variable)
{
    *traversal = SCRIPT_SOURCE_SELF;
    *variable = 0;
    if (script_analyzer_text_is(a, source, "self")) {
        return 0;
    }
    if (script_analyzer_is(source, "identifier")) {
        int symbol = script_analyzer_lookup(a, source);
        if (symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_QUERY_VARIABLE) {
            *variable = query_variable(a, source);
            return 0;
        }
        char *name = script_analyzer_name_copy(a, source);
        uint64_t relationship = query_relationship(a, source, name, false);
        free(name);
        return relationship;
    }
    if (script_analyzer_is(source, "member_expression")) {
        *variable = query_variable(a, script_analyzer_field(source, "object"));
        TSNode member = script_analyzer_field(source, "member");
        char *name = script_analyzer_name_copy(a, member);
        uint64_t relationship = query_relationship(a, member, name, false);
        free(name);
        return relationship;
    }
    TSNode function = script_analyzer_field(source, "function");
    TSNode arguments = script_analyzer_field(source, "arguments");
    TSNode relation = query_argument(arguments);
    bool up = script_analyzer_text_is(a, function, "ancestor");
    bool self_up = script_analyzer_text_is(a, function, "self_or_ancestor");
    if (!script_analyzer_is(source, "call_expression") || (!up && !self_up) ||
        ts_node_is_null(relation)) {
        script_analyzer_diagnostic(a, source, "Query source requires self, a query variable, an exclusive relationship, ancestor(Relationship) or self_or_ancestor(Relationship)");
        return 0;
    }
    char *name = script_analyzer_name_copy(a, relation);
    uint64_t relationship = query_relationship(a, relation, name, true);
    *traversal = up ? SCRIPT_SOURCE_ANCESTOR : SCRIPT_SOURCE_SELF_OR_ANCESTOR;
    free(name);
    return relationship;
}
static script_query query_membership(analyzer *a, TSNode node, bool relationship_allowed)
{
    script_query result = { .kind = SCRIPT_QUERY_HAS,
                            .range = script_analyzer_range(a, node) };
    TSNode name = node;
    if (script_analyzer_is(node, "member_expression")) {
        result.source_relationship = query_source(
            a,
            script_analyzer_field(node, "object"),
            &result.source_traversal,
            &result.source_variable
        );
        name = script_analyzer_field(node, "member");
        relationship_allowed &= !result.source_relationship;
    }
    char *text = script_analyzer_name_copy(a, name);
    int index = script_analyzer_find_type(a, node, text);
    uint64_t relationship = relationship_allowed ? query_relationship_id(a, text) : 0;
    if (index >= 0 && a->catalog->types[index].component) {
        result.component = a->catalog->types[index].component_id;
        script_analyzer_use(
            a,
            name,
            -1,
            SCRIPT_TYPE_USE,
            a->catalog->types[index].id
        );
    } else if (relationship && index < 0) {
        result.kind = SCRIPT_QUERY_RELATION;
        result.component = relationship;
    } else {
        script_analyzer_diagnostic(a, name, "Unknown/ambiguous query component '%s'", text);
    }
    free(text);
    return result;
}
static bool query_membership_node(analyzer *a, TSNode node)
{
    TSNode name = script_analyzer_is(node, "member_expression")
                    ? script_analyzer_field(node, "member")
                    : node;
    if (!script_analyzer_is(node, "identifier") &&
        !script_analyzer_is(node, "member_expression")) {
        return false;
    }
    if (script_analyzer_is(node, "identifier") && script_analyzer_lookup(a, node) >= 0) {
        return false;
    }
    char *text = script_analyzer_name_copy(a, name);
    int index = script_analyzer_find_type(a, node, text);
    free(text);
    return index >= 0 && a->catalog->types[index].component;
}
static void query_narrow(analyzer *a, int index, bool truth)
{
    const script_query *query = &a->out->queries[index];
    if (query->kind == SCRIPT_QUERY_VALUE) {
        script_analyzer_narrow_condition(a, query->value, truth);
        return;
    }
    if (query->kind == SCRIPT_QUERY_NOT && query->child_count == 1) {
        query_narrow(a, query->children[0], !truth);
        return;
    }
    if ((query->kind == SCRIPT_QUERY_ALL && truth) ||
        (query->kind == SCRIPT_QUERY_ANY && !truth)) {
        for (int i = 0; i < query->child_count; i++) {
            query_narrow(a, query->children[i], truth);
        }
    }
}
static void query_narrow_restore(analyzer *a, narrowing *saved)
{
    asetlen(a->narrowed, 0);
    for (int i = 0; i < acount(saved); i++) {
        apush(a->narrowed, saved[i]);
    }
    afree(saved);
}
static script_query query_changed(analyzer *a, TSNode node, TSNode arguments)
{
    script_query result = { .kind = SCRIPT_QUERY_CHANGED,
                            .range = script_analyzer_range(a, node) };
    TSNode argument = query_argument(arguments);
    if (ts_node_is_null(argument)) {
        script_analyzer_diagnostic(a, node, "changed requires one component or component binding");
        return result;
    }
    int symbol = script_analyzer_is(argument, "identifier")
                   ? script_analyzer_lookup(a, argument)
                   : -1;
    if (symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_COMPONENT) {
        const script_symbol *binding = &a->out->symbols[symbol];
        const script_catalog_type *
            type = script_catalog_type_by_id(a->catalog, binding->component_type.nominal);
        result.component = type ? type->component_id : 0;
        result.source_relationship = binding->source_relationship;
        result.source_traversal = binding->source_traversal;
        result.source_variable = binding->source_variable;
        script_analyzer_use(a, argument, symbol, SCRIPT_READ, 0);
        return result;
    }
    result = query_membership(a, argument, false);
    result.kind = SCRIPT_QUERY_CHANGED;
    result.range = script_analyzer_range(a, node);
    return result;
}
int script_analyzer_query_expression(analyzer *a, TSNode node)
{
    script_query result = { .range = script_analyzer_range(a, node) };
    if (script_analyzer_is(node, "parenthesized_expression")) {
        return script_analyzer_query_expression(a, script_analyzer_field(node, "expression"));
    }
    TSNode op = script_analyzer_field(node, "operator");
    if (script_analyzer_is(node, "unary_expression") &&
        (script_analyzer_text_is(a, op, "!") || script_analyzer_text_is(a, op, "not"))) {
        result.kind = SCRIPT_QUERY_NOT;
        apush(
            result.children,
            script_analyzer_query_expression(a, script_analyzer_field(node, "operand"))
        );
    } else if (
        script_analyzer_is(node, "binary_expression") &&
        (script_analyzer_text_is(a, op, "or") || script_analyzer_text_is(a, op, "||") ||
         script_analyzer_text_is(a, op, "and") || script_analyzer_text_is(a, op, "&&"))
    ) {
        bool any = script_analyzer_text_is(a, op, "or") ||
                   script_analyzer_text_is(a, op, "||");
        result.kind = any ? SCRIPT_QUERY_ANY : SCRIPT_QUERY_ALL;
        narrowing *saved = NULL;
        for (int i = 0; i < acount(a->narrowed); i++) {
            apush(saved, a->narrowed[i]);
        }
        int left = script_analyzer_query_expression(a, script_analyzer_field(node, "left"));
        apush(result.children, left);
        query_narrow(a, left, !any);
        apush(result.children, script_analyzer_query_expression(a, script_analyzer_field(node, "right")));
        query_narrow_restore(a, saved);
    } else if (query_membership_node(a, node)) {
        result = query_membership(a, node, false);
    } else {
        TSNode function = script_analyzer_field(node, "function");
        TSNode arguments = script_analyzer_field(node, "arguments");
        bool call = script_analyzer_is(node, "call_expression");
        if (call && script_analyzer_text_is(a, function, "has")) {
            TSNode argument = query_argument(arguments);
            if (ts_node_is_null(argument)) {
                script_analyzer_diagnostic(a, node, "has requires one component or relationship");
                result.kind = SCRIPT_QUERY_HAS;
            } else {
                result = query_membership(a, argument, true);
            }
        } else if (call && script_analyzer_text_is(a, function, "changed")) {
            result = query_changed(a, node, arguments);
        } else if (
            call && (script_analyzer_text_is(a, function, "all") ||
                     script_analyzer_text_is(a, function, "any") ||
                     script_analyzer_text_is(a, function, "not"))
        ) {
            result.kind = script_analyzer_text_is(a, function, "all") ? SCRIPT_QUERY_ALL
                        : script_analyzer_text_is(a, function, "any")
                            ? SCRIPT_QUERY_ANY
                            : SCRIPT_QUERY_NOT;
            narrowing *saved = NULL;
            for (int i = 0; i < acount(a->narrowed); i++) {
                apush(saved, a->narrowed[i]);
            }
            for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
                TSNode term = ts_node_named_child(arguments, i);
                if (ts_node_is_extra(term)) {
                    continue;
                }
                int child = script_analyzer_query_expression(a, term);
                apush(result.children, child);
                if (result.kind != SCRIPT_QUERY_NOT) {
                    query_narrow(a, child, result.kind == SCRIPT_QUERY_ALL);
                }
            }
            query_narrow_restore(a, saved);
            if (result.kind == SCRIPT_QUERY_NOT && acount(result.children) != 1) {
                script_analyzer_diagnostic(a, node, "not requires exactly one query term");
            }
        } else {
            result.kind = SCRIPT_QUERY_VALUE;
            result.value = script_analyzer_expression(a, node, script_analyzer_boolean_type());
            script_analyzer_condition_type(a, node, result.value);
            /* Presence conditions become ordinary boolean evaluator results. */
            if (script_analyzer_expr_type(a, result.value).flags & SCRIPT_TYPE_OPTIONAL) {
                for (int i = 0; i < 2; i++) {
                    result.value = script_analyzer_expr_add(
                        a,
                        (script_expression){
                            .kind = SCRIPT_EXPR_UNARY,
                            .op = SCRIPT_OP_NOT,
                            .type = script_analyzer_boolean_type(),
                            .range = result.range,
                            .left = result.value,
                            .right = -1,
                            .symbol = -1,
                            .effects = script_analyzer_expr_effects(a, result.value),
                        }
                    );
                }
            }
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
                         .range = script_analyzer_range(a, node) })
    );
    int index = acount(a->out->queries) - 1;
    if (!excluded) {
        return index;
    }
    script_query not = { .kind = SCRIPT_QUERY_NOT,
                         .range = script_analyzer_range(a, node) };
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
static int query_parameter(analyzer *a, TSNode parameter)
{
    TSNode type_node = script_analyzer_field(parameter, "type");
    script_type type = script_analyzer_type_node(a, type_node);
    script_type component_type = type;
    if (type.storage == PIN_POINTER) {
        const script_catalog_type *pointer = script_catalog_type_by_id(a->catalog, type.nominal);
        component_type = pointer ? pointer->element_type : script_analyzer_bad_type();
        component_type.flags |= type.flags & SCRIPT_TYPE_OPTIONAL;
    }
    const script_catalog_type
        *component = script_catalog_type_by_id(a->catalog, component_type.nominal);
    if (!component || !component->component) {
        script_analyzer_diagnostic(a, parameter, "Query binding requires a reflected component type or pointer to one");
    }
    if (type.container != PIN_CONTAINER_SINGLE ||
        component_type.container != PIN_CONTAINER_SINGLE) {
        script_analyzer_diagnostic(a, parameter, "Query binding requires a single component type");
    }
    int traversal = SCRIPT_SOURCE_SELF, variable = 0;
    TSNode source = script_analyzer_field(parameter, "source");
    uint64_t relationship = ts_node_is_null(source)
                              ? 0
                              : query_source(a, source, &traversal, &variable);
    int symbol = script_analyzer_symbol_new(
        a,
        script_analyzer_field(parameter, "name"),
        SCRIPT_COMPONENT,
        type,
        false
    );
    a->out->symbols[symbol].source_relationship = relationship;
    a->out->symbols[symbol].source_traversal = traversal;
    a->out->symbols[symbol].source_variable = variable;
    a->out->symbols[symbol].component_type = component_type;
    if (component && !(type.flags & SCRIPT_TYPE_OPTIONAL)) {
        int term = script_analyzer_query_component(a, parameter, component->component_id, false);
        a->out->queries[term].required = true;
        a->out->queries[term].source_relationship = relationship;
        a->out->queries[term].source_traversal = traversal;
        a->out->queries[term].source_variable = variable;
        script_analyzer_query_intersect(a, term);
    }
    return symbol;
}
static void query_generator(analyzer *a, TSNode node)
{
    TSNode source = script_analyzer_field(node, "source");
    if (!script_analyzer_is(source, "member_expression")) {
        script_analyzer_diagnostic(a, source, "Query generator requires source.Relationship");
        return;
    }
    int source_variable = query_variable(a, script_analyzer_field(source, "object"));
    TSNode relation = script_analyzer_field(source, "member");
    char *name = script_analyzer_name_copy(a, relation);
    uint64_t relationship = query_relationship_id(a, name);
    if (!relationship) {
        script_analyzer_diagnostic(a, relation, "Unknown query relationship '%s'", name);
    }
    free(name);
    /* Keep the frontend independent from ECS headers. This is the row ABI. */
    if (a->out->query_variable_count == 8) {
        script_analyzer_diagnostic(a, node, "Query supports at most eight target variables");
        return;
    }
    TSNode annotation = script_analyzer_field(node, "type");
    if (!ts_node_is_null(annotation)) {
        script_type actual = script_analyzer_type_node(a, annotation);
        script_type entity = { .storage = PIN_HANDLE,
                               .nominal = a->catalog->entity_type };
        script_analyzer_require(a, annotation, entity, actual);
    }
    int variable = ++a->out->query_variable_count;
    int symbol = script_analyzer_symbol_new(
        a,
        script_analyzer_field(node, "name"),
        SCRIPT_QUERY_VARIABLE,
        (script_type){ .storage = PIN_HANDLE, .nominal = a->catalog->entity_type },
        false
    );
    a->out->symbols[symbol].source_variable = variable;
    apush(
        a->out->queries,
        ((script_query){ .kind = SCRIPT_QUERY_GENERATOR,
                         .component = relationship,
                         .source_variable = source_variable,
                         .target_variable = variable,
                         .range = script_analyzer_range(a, node) })
    );
    script_analyzer_query_intersect(a, acount(a->out->queries) - 1);
}
static void query_capture(analyzer *a, int description, int expression)
{
    if (expression < 0 || a->out->descriptions[description].system) {
        return;
    }
    const script_expression value = a->out->expressions[expression];
    if (value.kind == SCRIPT_EXPR_ADDRESS) {
        int target = value.left;
        while (target >= 0 && a->out->expressions[target].kind == SCRIPT_EXPR_MEMBER) {
            target = a->out->expressions[target].left;
        }
        if (target >= 0 && a->out->expressions[target].kind == SCRIPT_EXPR_SYMBOL) {
            int symbol = a->out->expressions[target].symbol;
            if (symbol >= 0 &&
                a->out->symbols[symbol].query_description != description + 1) {
                script_diagnostic diagnostic = { .range = value.range };
                snprintf(diagnostic.message, sizeof diagnostic.message, "Captured query values are read-only; capture an existing pointer instead");
                apush(a->out->diagnostics, diagnostic);
            }
        }
    }
    if (value.kind == SCRIPT_EXPR_QUERY) {
        const script_query_description *nested = &a->out->descriptions[value.entry];
        for (int i = 0; i < acount(nested->captures); i++) {
            int symbol = nested->captures[i];
            if (a->out->symbols[symbol].query_description == description + 1) {
                continue;
            }
            bool captured = false;
            for (int j = 0; j < acount(a->out->descriptions[description].captures);
                 j++) {
                captured |= a->out->descriptions[description].captures[j] == symbol;
            }
            if (!captured) {
                apush(a->out->descriptions[description].captures, symbol);
            }
        }
    }
    if (value.kind == SCRIPT_EXPR_SYMBOL && value.symbol >= 0) {
        const script_symbol *symbol = &a->out->symbols[value.symbol];
        if (symbol->query_description != description + 1 &&
            value.symbol != a->out->owner_symbol &&
            (symbol->kind == SCRIPT_LOCAL || symbol->kind == SCRIPT_PARAMETER ||
             symbol->kind == SCRIPT_RESULT || symbol->kind == SCRIPT_STATE ||
             symbol->kind == SCRIPT_SHARED_STATE || symbol->kind == SCRIPT_COMPONENT ||
             symbol->kind == SCRIPT_QUERY_VARIABLE)) {
            script_query_description *query = &a->out->descriptions[description];
            for (int i = 0; i < acount(query->captures); i++) {
                if (query->captures[i] == value.symbol) {
                    return;
                }
            }
            apush(query->captures, value.symbol);
        }
    }
    query_capture(a, description, value.left);
    if (value.kind != SCRIPT_EXPR_CALL && value.kind != SCRIPT_EXPR_JOIN) {
        query_capture(a, description, value.right);
    }
    for (int i = 0; i < value.argument_count; i++) {
        query_capture(a, description, value.arguments[i].value);
    }
}
static void query_capture_term(analyzer *a, int description, int term)
{
    script_query value = a->out->queries[term];
    if (value.kind == SCRIPT_QUERY_VALUE) {
        query_capture(a, description, value.value);
    }
    for (int i = 0; i < value.child_count; i++) {
        query_capture_term(a, description, value.children[i]);
    }
}
static bool query_excludes_binding(const analyzer *a, int description, int term, bool truth)
{
    const script_query *query = &a->out->queries[term];
    if (query->kind == SCRIPT_QUERY_NOT && query->child_count == 1) {
        return query_excludes_binding(a, description, query->children[0], !truth);
    }
    if ((query->kind == SCRIPT_QUERY_ALL && truth) ||
        (query->kind == SCRIPT_QUERY_ANY && !truth)) {
        for (int i = 0; i < query->child_count; i++) {
            if (query_excludes_binding(a, description, query->children[i], truth)) {
                return true;
            }
        }
    }
    if (query->kind != SCRIPT_QUERY_HAS || truth) {
        return false;
    }
    const script_query_description *header = &a->out->descriptions[description];
    for (int i = 0; i < acount(header->symbols); i++) {
        const script_symbol *binding = &a->out->symbols[header->symbols[i]];
        const script_catalog_type *component = script_catalog_type_by_id(
            a->catalog,
            binding->component_type.nominal
        );
        if (binding->kind == SCRIPT_COMPONENT &&
            !(binding->type.flags & SCRIPT_TYPE_OPTIONAL) && component &&
            component->component_id == query->component &&
            binding->source_relationship == query->source_relationship &&
            binding->source_variable == query->source_variable &&
            binding->source_traversal == query->source_traversal) {
            return true;
        }
    }
    return false;
}
int script_analyzer_query_description(analyzer *a, TSNode node, bool system)
{
    int saved_scope = a->scope;
    narrowing *saved_narrowing = NULL;
    if (!system) {
        for (int i = 0; i < acount(a->narrowed); i++) {
            apush(saved_narrowing, a->narrowed[i]);
        }
    }
    if (!system) {
        a->scope = script_analyzer_scope_new(a, node);
    }
    int query_first = acount(a->out->queries);
    int saved_query = a->out->query, saved_variables = a->out->query_variable_count;
    a->out->query = -1;
    a->out->query_variable_count = 0;
    int description = acount(a->out->descriptions);
    int saved_description = a->query_description;
    a->query_description = description + 1;
    apush(
        a->out->descriptions,
        ((script_query_description){ .query = -1,
                                     .system = system,
                                     .range = script_analyzer_range(a, node) })
    );
    if (!system) {
        apush(
            a->out->symbols,
            ((script_symbol){
                .id = script_catalog_id("query-self", a->out->identity),
                .name = xstrdup("self"),
                .kind = SCRIPT_QUERY_VARIABLE,
                .type = { .storage = PIN_HANDLE, .nominal = a->catalog->entity_type },
                .scope = a->scope,
                .initializer = -1,
                .function = -1,
                .query_description = description + 1,
                .query_field = -1,
                .declaration = { .start = -1, .end = -1 } })
        );
    }
    TSNode list = script_analyzer_field(node, "parameters");
    bool saved_query_context = a->query;
    a->query = true;
    for (uint32_t i = 0; i < script_analyzer_named_count(list); i++) {
        TSNode entry = ts_node_named_child(list, i);
        if (ts_node_is_extra(entry)) {
            continue;
        }
        int symbol = -1, initializer = -1;
        TSNode binding_name = script_analyzer_field(entry, "name");
        if (system && !ts_node_is_null(binding_name) &&
            script_analyzer_text_is(a, binding_name, "dt")) {
            script_analyzer_diagnostic(a, binding_name, "'dt' is the implicit system step and cannot be a query binding");
        }
        if (script_analyzer_is(entry, "parameter")) {
            symbol = query_parameter(a, entry);
        } else if (script_analyzer_is(entry, "query_generator")) {
            int first = acount(a->out->symbols);
            query_generator(a, entry);
            if (acount(a->out->symbols) > first) {
                symbol = acount(a->out->symbols) - 1;
            }
        } else if (script_analyzer_is(entry, "query_binding")) {
            TSNode annotation = script_analyzer_field(entry, "type");
            script_type expected = ts_node_is_null(annotation)
                                     ? (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
                                     : script_analyzer_type_node(a, annotation);
            initializer = script_analyzer_expression(a, script_analyzer_field(entry, "value"), expected);
            script_type actual = script_analyzer_expr_type(a, initializer);
            if (ts_node_is_null(annotation)) {
                expected = actual;
                expected.flags &= ~(SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_LITERAL);
            } else {
                script_analyzer_require(a, entry, expected, actual);
            }
            symbol = script_analyzer_symbol_new(
                a,
                script_analyzer_field(entry, "name"),
                SCRIPT_LOCAL,
                expected,
                false
            );
            a->out->symbols[symbol].initializer = initializer;
            query_capture(a, description, initializer);
            /* Computed stages can fail before later structural guards. Keep
             * the structural plan aware that ordered evaluation is required. */
            int marker = script_analyzer_expr_add(
                a,
                (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                     .type = script_analyzer_boolean_type(),
                                     .constant = { .type = PIN_BOOL, .b = true },
                                     .range = script_analyzer_range(a, entry),
                                     .symbol = -1,
                                     .left = -1,
                                     .right = -1 }
            );
            apush(
                a->out->queries,
                ((script_query){ .kind = SCRIPT_QUERY_VALUE,
                                 .value = marker,
                                 .range = script_analyzer_range(a, entry) })
            );
            script_analyzer_query_intersect(a, acount(a->out->queries) - 1);
        } else if (script_analyzer_is(entry, "query_guard")) {
            int term = script_analyzer_query_expression(a, script_analyzer_field(entry, "condition"));
            query_capture_term(a, description, term);
            if (query_excludes_binding(a, description, term, true)) {
                script_analyzer_diagnostic(a, entry, "A component cannot be both a parameter and excluded");
            }
            apush(
                a->out->descriptions[description].stages,
                ((script_query_stage){ .symbol = -1,
                                       .expression = term,
                                       .range = script_analyzer_range(a, entry) })
            );
            query_narrow(a, term, true);
            /* Structural matching keeps its optimized plan, while the row evaluator
             * preserves source order for computed bindings and value guards. */
            script_analyzer_query_intersect(a, term);
        }
        if (symbol >= 0) {
            script_query_description *query = &a->out->descriptions[description];
            a->out->symbols[symbol].query_description = description + 1;
            a->out->symbols[symbol].query_field = acount(query->symbols);
            apush(query->symbols, symbol);
            apush(
                query->stages,
                ((script_query_stage){ .symbol = symbol,
                                       .expression = initializer,
                                       .range = script_analyzer_range(a, entry) })
            );
        }
    }
    a->query = saved_query_context;
    if (a->out->query < 0) {
        apush(a->out->queries, ((script_query){ .kind = SCRIPT_QUERY_ALL }));
        a->out->query = acount(a->out->queries) - 1;
    }
    script_query_description *query = &a->out->descriptions[description];
    for (int i = query_first; i < acount(a->out->queries); i++) {
        if (a->out->queries[i].description) {
            continue;
        }
        a->out->queries[i].description = description + 1;
        if (!system && a->out->queries[i].kind == SCRIPT_QUERY_CHANGED) {
            script_analyzer_diagnostic(a, node, "changed requires a scheduled system query");
        }
    }
    query->query = a->out->query;
    query->query_variable_count = a->out->query_variable_count;
    uint64_t shape = UINT64_C(14695981039346656037);
    for (int i = 0; i < acount(query->symbols); i++) {
        const script_symbol *field = &a->out->symbols[query->symbols[i]];
        const script_catalog_type
            *type = script_catalog_type_by_id(a->catalog, field->type.nominal);
        const script_catalog_type *component = script_catalog_type_by_id(
            a->catalog,
            field->component_type.nominal
        );
        char signature[256];
        snprintf(
            signature,
            sizeof signature,
            "%d:%d:%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%u:%" PRIu64 ":%" PRIu64,
            field->type.storage,
            field->type.container,
            field->type.nominal,
            field->type.parameter,
            field->type.collection,
            field->type.flags,
            type ? type->layout : 0,
            component ? component->layout : 0
        );
        shape ^= script_catalog_id(field->name, signature);
        shape *= UINT64_C(1099511628211);
    }
    char spelling[128];
    snprintf(
        spelling,
        sizeof spelling,
        "__query_row_%d_%d_%" PRIu64,
        query->range.document,
        query->range.start,
        shape
    );
    uint64_t id = script_catalog_id(a->out->identity, spelling);
    char *name = arena_alloc(&a->out->constants, strlen(spelling) + 1);
    strcpy(name, spelling);
    int count = acount(query->symbols);
    script_catalog_field *fields = arena_alloc(&a->out->constants, (size_t)count * sizeof *fields);
    for (int i = 0; i < count; i++) {
        const script_symbol *binding = &a->out->symbols[query->symbols[i]];
        fields[i] = (script_catalog_field){ .id = binding->id,
                                            .name = binding->name,
                                            .type = binding->type,
                                            .offset = (size_t)i,
                                            .writable = false };
    }
    query->row_type = (script_type){ .storage = PIN_QUERY,
                                     .nominal = id,
                                     .flags = SCRIPT_TYPE_QUERY_ROW };
    script_analyzer_publish_type(
        a,
        (script_catalog_type){ .id = id,
                               .name = name,
                               .source_name = name,
                               .type = query->row_type,
                               .fields = fields,
                               .field_count = count,
                               .size = sizeof(void *),
                               .internal = true }
    );
    if (!system) {
        a->out->query = saved_query;
        a->out->query_variable_count = saved_variables;
        query_narrow_restore(a, saved_narrowing);
    }
    a->scope = saved_scope;
    a->query_description = saved_description;
    return description;
}
void script_analyzer_system_parameters(analyzer *a, TSNode root)
{
    TSNode parameters = script_analyzer_field(root, "parameters");
    for (uint32_t i = 0; i < script_analyzer_named_count(parameters); i++) {
        if (!ts_node_is_extra(ts_node_named_child(parameters, i))) {
            a->out->system_description = script_analyzer_query_description(a, root, true) +
                                         1;
            return;
        }
    }
}
void script_analyzer_value_declarations(analyzer *a, TSNode body)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (script_analyzer_is(node, "state_declaration") ||
            script_analyzer_is(node, "action_declaration") ||
            script_analyzer_is(node, "event_declaration")) {
            script_analyzer_declare_value(a, node);
        } else if (script_analyzer_is(node, "function_declaration") || script_analyzer_is(node, "macro_declaration")) {
            script_analyzer_declare_function(a, node);
        } else if (script_analyzer_is(node, "constant_declaration")) {
            script_analyzer_declare_constant(a, node);
        } else if (script_analyzer_is(node, "local_declaration")) {
            int symbol = script_analyzer_symbol_new(
                a,
                script_analyzer_field(node, "name"),
                SCRIPT_LOCAL,
                script_analyzer_type_node(a, script_analyzer_field(node, "type")),
                true
            );
            apush(a->initializers, ((initializer){ symbol, script_analyzer_field(node, "value") }));
        } else if (script_analyzer_is(node, "exit_declaration")) {
            script_range range = script_analyzer_range(a, script_analyzer_field(node, "name"));
            bool declared = false;
            for (int j = 0; j < acount(a->out->symbols); j++) {
                if (a->out->symbols[j].kind == SCRIPT_LABEL &&
                    a->out->symbols[j].scope == a->scope &&
                    a->out->symbols[j].declaration.document == range.document &&
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
        }
    }
}
void script_analyzer_declaration_pass(analyzer *a, TSNode body)
{
    script_analyzer_record_types(a, body);
    script_analyzer_union_declarations(a, body);
    script_analyzer_value_declarations(a, body);
    script_analyzer_record_fields(a, body);
}
int script_analyzer_scope_at(const analyzer *a, TSNode body)
{
    script_range range = script_analyzer_range(a, body);
    for (int i = 0; i < acount(a->out->scopes); i++) {
        if (a->out->scopes[i].range.document == range.document &&
            a->out->scopes[i].range.start == range.start &&
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
            ((script_catalog_parameter){ .name = symbol->name,
                                         .unit = symbol->unit,
                                         .type = symbol->type })
        );
    }
    return payload;
}
/* A payload parameter the source does not name: a handler without a
 * parameter list binds each field by its own name, and one that lists fields
 * by name keeps an unnamed slot for each field it leaves out. */
static int hidden_payload(analyzer *a, int function, const script_catalog_parameter *field, const char *name)
{
    script_symbol hidden = { .name = xstrdup(name),
                             .unit = field->unit ? xstrdup(field->unit) : NULL,
                             .kind = SCRIPT_PARAMETER,
                             .id = script_catalog_id("payload-port", field->name),
                             .type = field->type,
                             .scope = a->scope,
                             .initializer = -1,
                             .function = function,
                             .declaration = { .start = -1, .end = -1 },
                             .writable = (field->type.flags &
                                          SCRIPT_TYPE_REFERENCE) != 0 };
    apush(a->out->symbols, hidden);
    return acount(a->out->symbols) - 1;
}
/* A payload parameter the source names, typed by field and checked against
 * a written type and unit; field is NULL past the end of the payload. */
static int payload_symbol(analyzer *a, TSNode parameter, const script_catalog_parameter *field)
{
    TSNode name = script_analyzer_is(parameter, "parameter")
                    ? script_analyzer_field(parameter, "name")
                    : parameter;
    script_type type = field ? field->type : script_analyzer_bad_type();
    TSNode annotation = script_analyzer_field(parameter, "type");
    char *unit = NULL;
    if (!ts_node_is_null(annotation)) {
        script_analyzer_require(a, annotation, type, script_analyzer_signature_type(a, annotation, &unit));
        if (field) {
            script_analyzer_signature_unit(a, annotation, field->unit, unit);
        }
    }
    int symbol = script_analyzer_symbol_new(
        a,
        name,
        SCRIPT_PARAMETER,
        type,
        (type.flags & SCRIPT_TYPE_REFERENCE) != 0
    );
    free(a->out->symbols[symbol].unit);
    a->out->symbols[symbol].unit = unit                 ? unit
                                 : field && field->unit ? xstrdup(field->unit)
                                                        : NULL;
    return symbol;
}
/* `after X(a, b)` binds the action's results in order, and `after X` binds
 * each by its own name. */
static void positional_payload(
    analyzer *a,
    int function,
    TSNode list,
    const script_catalog_parameter *payload,
    int count
)
{
    int ordinal = 0;
    for (uint32_t i = 0; i < script_analyzer_named_count(list); i++) {
        TSNode parameter = ts_node_named_child(list, i);
        if (ts_node_is_extra(parameter)) {
            continue;
        }
        int symbol = payload_symbol(a, parameter, ordinal < count ? &payload[ordinal] : NULL);
        apush(a->out->functions[function].parameters, symbol);
        ordinal++;
    }
    if (!script_analyzer_named_count(list)) {
        for (int i = 0; i < count; i++) {
            int symbol = hidden_payload(a, function, &payload[i], payload[i].name);
            apush(a->out->functions[function].parameters, symbol);
        }
    }
    if (script_analyzer_named_count(list) && ordinal != count) {
        script_analyzer_diagnostic(a, list, "Expected %d payload parameters, got %d", count, ordinal);
    }
}
/* `on X :: (a: T)` names payload fields in any order. Each listed field binds
 * a parameter of its written type; each other field an unnamed one, so the
 * parameters keep the payload's order and slots. */
void script_analyzer_named_payload(
    analyzer *a,
    int function,
    TSNode list,
    const script_catalog_parameter *payload,
    int count,
    const char *event
)
{
    TSNode *listed = NULL;
    for (int k = 0; k < count; k++) {
        apush(listed, (TSNode){ 0 });
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(list); i++) {
        TSNode parameter = ts_node_named_child(list, i);
        if (ts_node_is_extra(parameter)) {
            continue;
        }
        TSNode name = script_analyzer_field(parameter, "name");
        char *spelled = script_analyzer_name_copy(a, name);
        int k = 0;
        while (k < count && !script_name_matches(payload[k].name, spelled)) {
            k++;
        }
        if (k == count) {
            script_analyzer_diagnostic(a, name, "%s has no payload field '%s'", event, spelled);
        } else if (!ts_node_is_null(listed[k])) {
            script_analyzer_diagnostic(
                a,
                name,
                "Payload field '%s' is listed twice",
                payload[k].name
            );
        } else {
            listed[k] = parameter;
        }
        if (ts_node_is_null(script_analyzer_field(parameter, "type"))) {
            script_analyzer_diagnostic(a, name, "Write the type of payload field '%s'", spelled);
        }
        if (!ts_node_is_null(script_analyzer_field(parameter, "value")) ||
            !ts_node_is_null(script_analyzer_field(parameter, "source")) ||
            !ts_node_is_null(script_analyzer_field(parameter, "comptime"))) {
            script_analyzer_diagnostic(a, parameter, "A payload parameter takes no default, source or `$`");
        }
        free(spelled);
    }
    for (int k = 0; k < count; k++) {
        int symbol = ts_node_is_null(listed[k])
                       ? hidden_payload(a, function, &payload[k], "")
                       : payload_symbol(a, listed[k], &payload[k]);
        a->out->symbols[symbol].function = function;
        apush(a->out->functions[function].parameters, symbol);
    }
    afree(listed);
}
void script_analyzer_handler(analyzer *a, TSNode node)
{
    TSNode name_node = script_analyzer_field(node, "name"),
           body = script_analyzer_field(node, "body");
    char *name = script_analyzer_qualified_name(a, name_node);
    bool after = script_analyzer_text_is(a, script_analyzer_field(node, "kind"), "after");
    const script_catalog_capability *signature = NULL;
    int event_index = script_analyzer_find_capability(a, node, name, SCRIPT_CAPABILITY_EVENT);
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
        if (parent->symbol < 0 || a->out->symbols[parent->symbol].kind != SCRIPT_MACRO) {
            script_analyzer_diagnostic(a, node, "A function runs its own body; write the statements of `after entry` at the end of it");
        }
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
            if (script_analyzer_native_capability(a, candidate->id) &&
                candidate->kind == SCRIPT_CAPABILITY_EVENT &&
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
    int old_scope = a->scope, old_function = a->function;
    a->function = function;
    a->scope = script_analyzer_scope_new(a, body);
    a->out->functions[function].scope = a->scope;
    TSNode parameter_nodes = script_analyzer_field(node, "parameters");
    const script_catalog_parameter *payload = signature ? after ? signature->results
                                                                : signature->parameters
                                                        : NULL;
    int count = signature ? after ? signature->result_count : signature->parameter_count
                          : 0;
    if (!after) {
        script_analyzer_named_payload(a, function, parameter_nodes, payload, count, name);
    } else {
        positional_payload(a, function, parameter_nodes, payload, count);
    }
    /* The payload aggregate has a semantic identity. An `on` handler names its
     * fields, so reading the aggregate there is an error. */
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
    if (expression->kind == SCRIPT_EXPR_DEREFERENCE) {
        return parent;
    }
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
    script_binding result = { .range = script_analyzer_range(a, node) };
    result.target = script_analyzer_expression(
        a,
        script_analyzer_field(node, "target"),
        (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
    );
    script_expression target = a->out->expressions[result.target];
    if (!target.writable || target.kind != SCRIPT_EXPR_MEMBER) {
        script_analyzer_diagnostic(a, script_analyzer_field(node, "target"), "Binding requires a writable reflected component field");
    }
    int base = result.target;
    while (base >= 0 && (a->out->expressions[base].kind == SCRIPT_EXPR_MEMBER ||
                         a->out->expressions[base].kind == SCRIPT_EXPR_DEREFERENCE)) {
        base = a->out->expressions[base].left;
    }
    if (base >= 0 && a->out->expressions[base].kind == SCRIPT_EXPR_SYMBOL &&
        a->out->expressions[base].symbol >= 0 &&
        (a->out->symbols[a->out->expressions[base].symbol].source_relationship ||
         a->out->symbols[a->out->expressions[base].symbol].source_variable)) {
        script_analyzer_diagnostic(a, node, "Reactive binding destination must belong to self; write query sources in the system body");
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
    return script_analyzer_is(node, "record_declaration") ||
           script_analyzer_is(node, "union_declaration") ||
           script_analyzer_is(node, "state_declaration") ||
           script_analyzer_is(node, "action_declaration") ||
           script_analyzer_is(node, "event_declaration") ||
           script_analyzer_is(node, "constant_declaration") ||
           script_analyzer_is(node, "function_declaration") ||
           script_analyzer_is(node, "macro_declaration") ||
           script_analyzer_is(node, "handler") ||
           script_analyzer_is(node, "events_declaration") ||
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
        } else if (script_analyzer_is(node, "events_declaration")) {
            int old = a->scope;
            a->scope = script_analyzer_scope_at(a, script_analyzer_field(node, "body"));
            script_analyzer_definition_pass(a, script_analyzer_field(node, "body"));
            a->scope = old;
        } else if (script_analyzer_is(node, "entry_declaration")) {
            script_range range = script_analyzer_range(a, node);
            int function = -1;
            for (int j = 0; j < acount(a->out->functions); j++) {
                if (a->out->functions[j].kind == SCRIPT_FUNCTION_MACRO_ENTRY &&
                    a->out->functions[j].parent == a->function &&
                    a->out->functions[j].range.document == range.document &&
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
