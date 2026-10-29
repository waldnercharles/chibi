#include "language/callable_headers.h"
#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/record.h"
#include "types/scalar.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPECIALIZATIONS 256
#define COMPILE_STEPS 65536

static TSNode binder(TSNode node)
{
    if (script_analyzer_is(node, "type_binder")) {
        return node;
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        TSNode found = binder(ts_node_named_child(node, i));
        if (!ts_node_is_null(found)) {
            return found;
        }
    }
    return (TSNode){ 0 };
}
/* The `T` of a scoped `^T` parameter, which binds like a `T` parameter. */
static TSNode scoped_target(analyzer *a, TSNode parameter)
{
    TSNode list = ts_node_parent(parameter);
    for (uint32_t i = 0; i < script_analyzer_named_count(list); i++) {
        if (ts_node_eq(ts_node_named_child(list, i), parameter)) {
            const char *source = script_analyzer_syntax(a, parameter)->text;
            return script_scoped_pointer_target(source, ts_node_parent(list), i);
        }
    }
    return (TSNode){ 0 };
}
static int template_symbol(const analyzer *a, int symbol)
{
    for (int i = 0; i < acount(a->templates); i++) {
        if (a->templates[i].symbol == symbol) {
            return i;
        }
    }
    return -1;
}
static bool type_result(analyzer *a, TSNode node)
{
    TSNode result = script_analyzer_field(node, "results");
    return script_analyzer_text_is(a, result, "Type");
}
static bool generic(analyzer *a, TSNode node)
{
    if (type_result(a, node) ||
        !ts_node_is_null(binder(script_analyzer_field(node, "parameters")))) {
        return true;
    }
    TSNode parameters = script_analyzer_field(node, "parameters");
    for (uint32_t i = 0; i < script_analyzer_named_count(parameters); i++) {
        TSNode parameter = ts_node_named_child(parameters, i);
        if (!ts_node_is_null(script_analyzer_field(parameter, "comptime")) ||
            !ts_node_is_null(
                script_callable_type_node(script_analyzer_field(parameter, "type"))
            )) {
            return true;
        }
    }
    return false;
}
bool script_analyzer_generic_declare(analyzer *a, TSNode node)
{
    if (!generic(a, node)) {
        return false;
    }
    char *name = script_analyzer_name_copy(a, script_analyzer_field(node, "name"));
    int symbol = script_analyzer_callable_symbol(a, name);
    free(name);
    if (template_symbol(a, symbol) >= 0) {
        return true;
    }
    TSNode parameters = script_analyzer_field(node, "parameters");
    for (uint32_t i = 0; i < script_analyzer_named_count(parameters); i++) {
        TSNode parameter = ts_node_named_child(parameters, i);
        TSNode annotation = script_analyzer_field(parameter, "type");
        TSNode callable = script_callable_type_node(annotation);
        if (ts_node_is_null(callable)) {
            continue;
        }
        if (!script_callable_type_annotation(annotation)) {
            script_analyzer_diagnostic(a, annotation, "Callable parameters cannot be optional, stored in containers or used through pointers");
        }
        TSNode inputs = script_analyzer_field(callable, "parameters");
        for (uint32_t p = 0; p < script_analyzer_named_count(inputs); p++) {
            TSNode input = ts_node_named_child(inputs, p);
            if (ts_node_is_extra(input)) {
                continue;
            }
            if (ts_node_is_null(script_analyzer_field(input, "type")) ||
                !ts_node_is_null(script_analyzer_field(input, "value")) ||
                !ts_node_is_null(script_analyzer_field(input, "source")) ||
                !ts_node_is_null(script_analyzer_field(input, "comptime"))) {
                script_analyzer_diagnostic(a, input, "Callable signatures require typed parameters without defaults or source bindings");
            }
        }
        TSNode results = script_analyzer_field(callable, "results");
        int count = 0;
        if (script_analyzer_is(results, "parameter_list")) {
            for (uint32_t r = 0; r < script_analyzer_named_count(results); r++) {
                count += !ts_node_is_extra(ts_node_named_child(results, r));
            }
        }
        if (count > 1) {
            script_analyzer_diagnostic(a, results, "Callable parameters currently support at most one result");
        }
        if (!ts_node_is_null(script_analyzer_field(parameter, "value")) ||
            !ts_node_is_null(script_analyzer_field(parameter, "source"))) {
            script_analyzer_diagnostic(a, parameter, "Callable parameters take no default value or source binding");
        }
    }
    symbol = script_analyzer_symbol_new(
        a,
        script_analyzer_field(node, "name"),
        SCRIPT_FUNCTION,
        (script_type){ .storage = PIN_FLOW },
        false
    );
    apush(
        a->templates,
        ((generic_template){ .node = node,
                             .symbol = symbol,
                             .scope = a->scope,
                             .type_result = type_result(a, node) })
    );
    script_analyzer_generic_export(a, node, symbol);
    return true;
}
void script_analyzer_generic_prepare(analyzer *a, TSNode body)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        if (script_analyzer_is(node, "function_declaration")) {
            script_analyzer_generic_declare(a, node);
        } else if (script_analyzer_is(node, "record_declaration") && !ts_node_is_null(script_analyzer_field(node, "parameters"))) {
            int symbol = script_analyzer_symbol_new(
                a,
                script_analyzer_field(node, "name"),
                SCRIPT_TYPE,
                (script_type){ .storage = PIN_STRUCT },
                false
            );
            script_analyzer_generic_export(a, node, symbol);
            apush(
                a->templates,
                ((
                    generic_template
                ){ .node = node, .symbol = symbol, .scope = a->scope, .record = true })
            );
        }
    }
}
static script_type concrete(script_type type)
{
    type.flags &= ~(
        SCRIPT_TYPE_LITERAL | SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_GENERIC | SCRIPT_TYPE_REFERENCE
    );
    if (!(type.flags & SCRIPT_TYPE_UNION)) {
        type.parameter = 0;
    }
    return type;
}
static bool outer_optional(analyzer *a, TSNode type)
{
    if (script_analyzer_is(type, "parenthesized_type")) {
        return outer_optional(a, script_analyzer_field(type, "type"));
    }
    for (uint32_t i = script_analyzer_child_count(type); i > 0; i--) {
        TSNode child = ts_node_child(type, i - 1);
        if (ts_node_is_extra(child)) {
            continue;
        }
        return script_analyzer_text_is(a, child, "?") ||
               (script_analyzer_is(child, "parenthesized_type") &&
                outer_optional(a, child));
    }
    return false;
}
static script_type binding_type(script_type actual, bool optional)
{
    if (optional) {
        actual.flags &= ~(SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
    }
    return actual;
}
static uint64_t compile_bits(pin_value value)
{
    if (value.type == PIN_F32) {
        uint32_t bits;
        memcpy(&bits, &value.f, sizeof bits);
        return bits;
    }
    if (value.type == PIN_F64) {
        uint64_t bits;
        memcpy(&bits, &value.d, sizeof bits);
        return bits;
    }
    return value.type == PIN_FLOW ? 0 : behavior_scalar_bits(value);
}
static bool equal_value(compile_value x, compile_value y)
{
    if (x.is_type != y.is_type || x.is_callable != y.is_callable) {
        return false;
    }
    if (x.is_callable) {
        return x.callable == y.callable;
    }
    if (x.is_type) {
        return script_type_equal(x.type, y.type) &&
               !((x.type.flags ^ y.type.flags) & SCRIPT_TYPE_ERROR_ALTERNATIVE);
    }
    if (x.value.type != y.value.type) {
        return false;
    }
    return behavior_scalar_string(x.value.type)
             ? pin_text_equal(pin_text(x.value), pin_text(y.value))
             : compile_bits(x.value) == compile_bits(y.value);
}
static int binding(compile_binding *bindings, const char *name)
{
    for (int i = 0; i < acount(bindings); i++) {
        if (!strcmp(bindings[i].name, name)) {
            return i;
        }
    }
    return -1;
}
static bool bind(analyzer *a, TSNode node, compile_binding **bindings, const char *name, compile_value value)
{
    int old = binding(*bindings, name);
    if (old >= 0) {
        if (!equal_value((*bindings)[old].value, value)) {
            script_analyzer_diagnostic(a, node, "Conflicting polymorphic argument '%s'", name);
            return false;
        }
        return true;
    }
    apush(*bindings, ((compile_binding){ xstrdup(name), value }));
    return true;
}
static bool same_bindings(compile_binding *x, compile_binding *y)
{
    if (acount(x) != acount(y)) {
        return false;
    }
    for (int i = 0; i < acount(x); i++) {
        if (strcmp(x[i].name, y[i].name) || !equal_value(x[i].value, y[i].value)) {
            return false;
        }
    }
    return true;
}
static int by_name(const void *x, const void *y)
{
    return strcmp(
        ((const compile_binding *)x)->name,
        ((const compile_binding *)y)->name
    );
}
static uint64_t mix(uint64_t hash, uint64_t value)
{
    for (int i = 0; i < 8; i++) {
        hash = (hash ^ (value & 255)) * UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}
static uint64_t binding_id(uint64_t definition, compile_binding *bindings, bool evaluate)
{
    uint64_t hash = mix(
        script_catalog_id(evaluate ? "compile-time-invocation" : "polymorphic-instance", ""),
        definition
    );
    for (int i = 0; i < acount(bindings); i++) {
        hash = mix(hash, script_catalog_id("argument", bindings[i].name));
        compile_value value = bindings[i].value;
        hash = mix(hash, value.is_type);
        if (value.is_callable) {
            hash = mix(hash, script_catalog_id("callable-binding", ""));
            hash = mix(hash, value.callable);
        } else if (value.is_type) {
            hash = mix(hash, script_type_tag(value.type));
            if (value.type.flags & SCRIPT_TYPE_ERROR_ALTERNATIVE) {
                hash = mix(hash, script_catalog_id("error-alternative", ""));
            }
        } else {
            hash = mix(hash, value.value.type);
            hash = mix(
                hash,
                behavior_scalar_string(value.value.type)
                    ? script_catalog_id_bytes(
                          "constant-string",
                          pin_text(value.value).bytes,
                          pin_text(value.value).length
                      )
                    : compile_bits(value.value)
            );
        }
    }
    return hash ? hash : 1;
}
static uint64_t specialization_id(const analyzer *a, generic_template definition, compile_binding *bindings)
{
    return binding_id(a->out->symbols[definition.symbol].id, bindings, false);
}
static void free_bindings(compile_binding *bindings)
{
    for (int i = 0; i < acount(bindings); i++) {
        free(bindings[i].name);
    }
    afree(bindings);
}
static void install(analyzer *a, TSNode node, compile_binding *bindings)
{
    for (int i = 0; i < acount(bindings); i++) {
        compile_value value = bindings[i].value;
        TSNode origin = node;
        TSNode parameters = script_analyzer_field(node, "parameters");
        for (uint32_t j = 0; j < script_analyzer_named_count(parameters); j++) {
            TSNode parameter = ts_node_named_child(parameters, j);
            if (ts_node_is_extra(parameter)) {
                continue;
            }
            TSNode candidate = script_analyzer_field(parameter, "name");
            char *name = script_analyzer_name_copy(a, candidate);
            bool match = !strcmp(name, bindings[i].name);
            free(name);
            if (match) {
                origin = candidate;
                break;
            }
            TSNode inferred = binder(script_analyzer_field(parameter, "type"));
            if (!ts_node_is_null(inferred)) {
                candidate = script_analyzer_field(inferred, "name");
                name = script_analyzer_name_copy(a, candidate);
                match = !strcmp(name, bindings[i].name);
                free(name);
                if (match) {
                    origin = candidate;
                    break;
                }
            }
        }
        if (value.is_callable) {
            int alias = script_analyzer_result_new(
                a,
                node,
                bindings[i].name,
                (script_type){ .storage = PIN_FLOW }
            );
            script_symbol *symbol = &a->out->symbols[alias];
            symbol->kind = SCRIPT_FUNCTION;
            symbol->callable_binding = true;
            symbol->id = symbol->capability = value.callable;
            symbol->writable = false;
            symbol->function = -1;
            for (int j = 0; j < acount(a->out->functions); j++) {
                int declared = a->out->functions[j].symbol;
                if (declared >= 0 && a->out->symbols[declared].id == value.callable) {
                    symbol->function = j;
                    break;
                }
            }
            continue;
        }
        /* The aliases live in their own instantiation scope. */
        int symbol = script_analyzer_result_new(
            a,
            node,
            bindings[i].name,
            value.is_type ? value.type : (script_type){ .storage = value.value.type }
        );
        a->out->symbols[symbol].kind = value.is_type ? SCRIPT_TYPE : SCRIPT_CONSTANT;
        a->out->symbols[symbol].declaration = script_analyzer_range(a, origin);
        a->out->symbols[symbol].writable = false;
        if (!value.is_type) {
            a->out->symbols[symbol].initializer = script_analyzer_expr_add(
                a,
                (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                     .type = { .storage = value.value.type },
                                     .constant = value.value,
                                     .constant_snapshot = true,
                                     .symbol = -1,
                                     .left = -1,
                                     .right = -1,
                                     .range = script_analyzer_range(a, origin) }
            );
        }
    }
}
/* A callable parameter is a compile-time binding to one exact signature. */
bool script_analyzer_callable_argument(
    analyzer *a,
    TSNode value,
    TSNode signature,
    compile_binding *bindings,
    int definition_scope,
    compile_value *out
)
{
    int function = -1;
    uint64_t id = 0;
    const script_catalog_capability *capability = NULL;
    if (script_analyzer_is(value, "lambda_expression")) {
        function = script_analyzer_lambda(a, value);
        if (function >= 0) {
            id = a->out->symbols[a->out->functions[function].symbol].id;
        }
    } else if (script_analyzer_is(value, "identifier") || script_analyzer_is(value, "member_expression")) {
        char *name = script_analyzer_qualified_name(a, value);
        int symbol = script_analyzer_import_namespace(a, value, name)
                       ? script_analyzer_package_function(a, value, name)
                       : script_analyzer_callable_symbol(a, name);
        if (symbol >= 0) {
            function = a->out->symbols[symbol].function;
            id = a->out->symbols[symbol].capability;
        }
        if (!id) {
            int index = script_analyzer_find_capability(a, value, name, SCRIPT_CAPABILITY_FUNCTION);
            if (index >= 0) {
                capability = &a->catalog->capabilities[index];
                id = capability->id;
            }
        } else if (function < 0) {
            capability = script_catalog_capability_by_id(a->catalog, id);
        }
        if (symbol >= 0 && function < 0 && !capability) {
            script_analyzer_diagnostic(a, value, "Pass a concrete function; generic callbacks need concrete parameters");
            id = 0;
        }
        if (id) {
            script_analyzer_use(a, value, symbol, SCRIPT_CALL, id);
        }
        free(name);
    }
    if (!id) {
        script_analyzer_diagnostic(a, value, "Callable argument requires a named function or capture-free inline function");
        return false;
    }
    if (function >= 0 &&
        (a->out->functions[function].kind != SCRIPT_FUNCTION_AUTHORED ||
         a->out->symbols[a->out->functions[function].symbol].kind != SCRIPT_FUNCTION)) {
        script_analyzer_diagnostic(a, value, "Callable arguments require ordinary functions");
        return false;
    }
    int previous_scope = a->scope;
    a->scope = definition_scope;
    a->scope = script_analyzer_scope_new(a, signature);
    install(a, signature, bindings);
    bool valid = true;
    for (int result = 0; result < 2; result++) {
        TSNode list = script_analyzer_field(signature, result ? "results" : "parameters");
        bool single = result && !ts_node_is_null(list) &&
                      !script_analyzer_is(list, "parameter_list");
        int expected = single ? 1 : (int)script_analyzer_named_count(list);
        int actual = function >= 0
                       ? acount(
                             result ? a->out->functions[function].results
                                    : a->out->functions[function].parameters
                         )
                   : result ? capability->result_count
                            : capability->parameter_count;
        int ordinal = 0;
        for (int i = 0; i < expected; i++) {
            TSNode parameter = single ? list
                                      : ts_node_named_child(list, (uint32_t)i);
            if (ts_node_is_extra(parameter)) {
                continue;
            }
            TSNode type_node = single ? parameter
                                      : script_analyzer_field(parameter, "type");
            script_type wanted = script_analyzer_type_node(a, type_node);
            if (ordinal < actual) {
                script_type
                    supplied = function >= 0
                                 ? a->out
                                       ->symbols[(
                                           result ? a->out->functions[function].results
                                                  : a->out->functions[function].parameters
                                       )[ordinal]]
                                       .type
                                 : (result ? capability->results
                                           : capability->parameters)[ordinal]
                                       .type;
                /* Scoped ^T inputs use a reference to T internally. The
                 * callable contract compares their source pointer types. */
                if (!result && (supplied.flags & SCRIPT_TYPE_REFERENCE)) {
                    supplied.flags &= ~SCRIPT_TYPE_REFERENCE;
                    supplied = script_analyzer_pointer_type(a, supplied);
                }
                if (!script_type_equal(wanted, supplied)) {
                    script_analyzer_diagnostic(
                        a,
                        value,
                        "Callable %s %d has a different type from its "
                        "parameter signature",
                        result ? "result" : "parameter",
                        ordinal + 1
                    );
                    valid = false;
                }
            }
            ordinal++;
        }
        if (ordinal != actual) {
            script_analyzer_diagnostic(
                a,
                value,
                "Callable signature requires %d %s, got %d",
                ordinal,
                result ? "results" : "parameters",
                actual
            );
            valid = false;
        }
    }
    a->scope = previous_scope;
    unsigned effects = function >= 0 ? a->out->functions[function].effects
                                     : capability->effects;
    if (effects & SCRIPT_EFFECT_SUSPEND) {
        script_analyzer_diagnostic(a, value, "Callable arguments must finish synchronously");
        valid = false;
    }
    *out = (compile_value){ .is_callable = true, .callable = id };
    return valid;
}
static TSNode parameter_at(TSNode parameters, int ordinal)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(parameters); i++) {
        TSNode p = ts_node_named_child(parameters, i);
        if (!ts_node_is_extra(p) && ordinal-- == 0) {
            return p;
        }
    }
    return (TSNode){ 0 };
}
static TSNode parameter_named(analyzer *a, TSNode parameters, TSNode name)
{
    char *wanted = script_analyzer_name_copy(a, name);
    TSNode found = { 0 };
    for (uint32_t i = 0; i < script_analyzer_named_count(parameters); i++) {
        TSNode p = ts_node_named_child(parameters, i);
        char *candidate = script_analyzer_name_copy(a, script_analyzer_field(p, "name"));
        bool match = !strcmp(candidate, wanted);
        free(candidate);
        if (match) {
            found = p;
            break;
        }
    }
    free(wanted);
    return found;
}
static int instantiate(analyzer *a, int index, compile_binding *bindings)
{
    generic_template definition = a->templates[index];
    int old_scope = a->scope, old_function = a->function;
    bool old_instantiating = a->instantiating;
    uint64_t old_specialization = a->specialization;
    narrowing *old_narrowed = a->narrowed;
    TSNode old_awaited = a->awaited;
    int old_loops = a->loops;
    a->narrowed = NULL;
    a->awaited = (TSNode){ 0 };
    a->loops = 0;
    a->specialization = specialization_id(a, definition, bindings);
    a->scope = definition.scope;
    a->function = -1;
    a->scope = script_analyzer_scope_new(a, definition.node);
    install(a, definition.node, bindings);
    a->instantiating = true;
    int function = acount(a->out->functions);
    script_analyzer_declare_function(a, definition.node);
    int symbol = a->out->functions[function].symbol;
    a->out->symbols[symbol].id = a->specialization;
    a->out->symbols[symbol].capability = a->specialization;
    a->instantiating = old_instantiating;
    apush(
        a->instances,
        ((generic_instance){ .template = index, .function = function, .bindings = bindings })
    );
    for (int i = 0; i < acount(a->bodies); i++) {
        if (a->bodies[i].function == function) {
            a->bodies[i].state = BODY_ACTIVE;
            script_analyzer_callable_definition(
                a,
                function,
                script_analyzer_field(definition.node, "body")
            );
            script_analyzer_function_type(a, function);
            a->bodies[i].state = BODY_DONE;
            break;
        }
    }
    a->scope = old_scope;
    a->function = old_function;
    a->specialization = old_specialization;
    afree(a->narrowed);
    a->narrowed = old_narrowed;
    a->awaited = old_awaited;
    a->loops = old_loops;
    return function;
}
int script_analyzer_generic_call(analyzer *a, TSNode call, int symbol, int **resolved)
{
    int index = template_symbol(a, symbol);
    if (index < 0) {
        return -1;
    }
    generic_template definition = a->templates[index];
    if (definition.record || definition.type_result) {
        script_analyzer_diagnostic(a, call, "Type construction requires a compile-time type context");
        return -1;
    }
    TSNode parameters = script_analyzer_field(definition.node, "parameters");
    TSNode arguments = script_analyzer_field(call, "arguments");
    int count = (int)script_analyzer_named_count(arguments), ordinal = 0;
    int *values = xmalloc((size_t)(count + 1) * sizeof *values);
    compile_binding *bindings = NULL;
    bool valid = true;
    for (int i = 0; i < count; i++) {
        values[i] = -1;
        TSNode argument = ts_node_named_child(arguments, (uint32_t)i);
        if (ts_node_is_extra(argument)) {
            continue;
        }
        TSNode label = script_analyzer_field(argument, "name");
        TSNode parameter = ts_node_is_null(label)
                             ? parameter_at(parameters, ordinal++)
                             : parameter_named(a, parameters, label);
        TSNode value = ts_node_is_null(label)
                         ? argument
                         : script_analyzer_field(argument, "value");
        if (ts_node_is_null(value)) {
            value = argument;
        }
        if (ts_node_is_null(parameter)) {
            script_analyzer_diagnostic(a, argument, "Unknown polymorphic argument");
            valid = false;
            continue;
        }
        TSNode target = scoped_target(a, parameter);
        TSNode type = ts_node_is_null(target)
                        ? script_analyzer_field(parameter, "type")
                        : target;
        TSNode callable_type = script_callable_type_node(type);
        if (!ts_node_is_null(callable_type)) {
            values[i] = -2;
            continue;
        }
        TSNode type_binder = binder(type);
        char *name = script_analyzer_name_copy(
            a,
            ts_node_is_null(type_binder) ? script_analyzer_field(type, "name")
                                         : script_analyzer_field(type_binder, "name")
        );
        bool constant = !ts_node_is_null(script_analyzer_field(parameter, "comptime"));
        if (constant) {
            compile_value result;
            int budget = COMPILE_STEPS;
            script_type expected = script_analyzer_type_node(a, type);
            bool ok = script_analyzer_compile_as(a, value, NULL, expected.storage, &result, &budget);
            if (ok && !result.is_type && result.value.type != expected.storage) {
                ok = behavior_scalar_cast_supported(result.value.type, expected.storage);
                if (ok) {
                    behavior_scalar_cast(
                        result.value,
                        expected.storage,
                        BEHAVIOR_CAST_WRAP,
                        &result.value
                    );
                }
            }
            char *parameter_name = script_analyzer_name_copy(a, script_analyzer_field(parameter, "name"));
            valid &= ok && bind(a, value, &bindings, parameter_name, result);
            free(parameter_name);
            values[i] = -2;
        } else {
            int previous = binding(bindings, name);
            script_type expected = previous >= 0 && bindings[previous].value.is_type
                                     ? bindings[previous].value.type
                                     : (script_type){ .flags = SCRIPT_TYPE_UNTYPED };
            values[i] = ts_node_is_null(target)
                          ? script_analyzer_expression(a, value, expected)
                          : script_analyzer_scoped_argument(a, value, expected);
            script_type actual = concrete(script_analyzer_expr_type(a, values[i]));
            if (!ts_node_is_null(type_binder) || previous >= 0) {
                actual = binding_type(actual, outer_optional(a, type));
                if (script_analyzer_token_present(a, type, "[") ||
                    script_analyzer_token_present(a, type, "[]")) {
                    bool view = script_analyzer_token_present(a, type, "view");
                    pin_container container = view ? PIN_CONTAINER_VIEW
                                                   : PIN_CONTAINER_ARRAY;
                    if (actual.container != container) {
                        script_analyzer_diagnostic(
                            a,
                            value,
                            view ? "Polymorphic view argument requires a view" : "Polymorphic array argument requires an array"
                        );
                        valid = false;
                    }
                    actual = script_catalog_element_type(a->catalog, actual);
                }
                valid &= bind(a, value, &bindings, name, (compile_value){ .is_type = true, .type = actual });
            }
        }
        free(name);
    }
    for (uint32_t p = 0; p < script_analyzer_named_count(parameters); p++) {
        TSNode parameter = ts_node_named_child(parameters, p);
        if (ts_node_is_null(
                script_callable_type_node(script_analyzer_field(parameter, "type"))
            )) {
            continue;
        }
        int matches = 0, position = 0;
        for (int i = 0; i < count; i++) {
            TSNode argument = ts_node_named_child(arguments, (uint32_t)i);
            if (ts_node_is_extra(argument)) {
                continue;
            }
            TSNode label = script_analyzer_field(argument, "name");
            TSNode selected = ts_node_is_null(label)
                                ? parameter_at(parameters, position++)
                                : parameter_named(a, parameters, label);
            matches += ts_node_eq(parameter, selected);
        }
        if (matches != 1) {
            char *name = script_analyzer_name_copy(a, script_analyzer_field(parameter, "name"));
            script_analyzer_diagnostic(a, call, "Pass callable argument '%s' exactly once", name);
            free(name);
            valid = false;
        }
    }
    ordinal = 0;
    for (int i = 0; i < count; i++) {
        TSNode argument = ts_node_named_child(arguments, (uint32_t)i);
        if (ts_node_is_extra(argument)) {
            continue;
        }
        TSNode label = script_analyzer_field(argument, "name");
        TSNode parameter = ts_node_is_null(label)
                             ? parameter_at(parameters, ordinal++)
                             : parameter_named(a, parameters, label);
        TSNode signature = script_callable_type_node(
            script_analyzer_field(parameter, "type")
        );
        if (ts_node_is_null(signature)) {
            continue;
        }
        TSNode value = ts_node_is_null(label)
                         ? argument
                         : script_analyzer_call_argument(argument);
        compile_value callable = { 0 };
        char *parameter_name = script_analyzer_name_copy(a, script_analyzer_field(parameter, "name"));
        bool ok = script_analyzer_callable_argument(
            a,
            value,
            signature,
            bindings,
            definition.scope,
            &callable
        );
        valid &= ok && bind(a, value, &bindings, parameter_name, callable);
        free(parameter_name);
    }
    asort(bindings, by_name);
    if (!valid) {
        free(values);
        free_bindings(bindings);
        return -1;
    }
    for (int i = 0; i < acount(a->instances); i++) {
        if (a->instances[i].template == index &&
            same_bindings(a->instances[i].bindings, bindings)) {
            free_bindings(bindings);
            *resolved = values;
            return a->instances[i].function;
        }
    }
    if (acount(a->instances) >= SPECIALIZATIONS) {
        script_analyzer_diagnostic(a, call, "Polymorphic specialization budget exceeded");
        free(values);
        free_bindings(bindings);
        return -1;
    }
    *resolved = values;
    return instantiate(a, index, bindings);
}

int script_analyzer_callable_parameter_index(analyzer *a, int symbol, const char *name)
{
    if (symbol < 0 || !a->out->symbols[symbol].callable_binding) {
        return -2;
    }
    const script_symbol alias = a->out->symbols[symbol];
    for (int i = 0; i < acount(a->instances); i++) {
        const generic_instance instance = a->instances[i];
        if (instance.function < 0 ||
            a->out->scopes[a->out->functions[instance.function].scope].parent !=
                alias.scope) {
            continue;
        }
        TSNode parameters = script_analyzer_field(
            a->templates[instance.template].node,
            "parameters"
        );
        for (uint32_t p = 0; p < script_analyzer_named_count(parameters); p++) {
            TSNode parameter = ts_node_named_child(parameters, p);
            if (!script_analyzer_text_is(
                    a,
                    script_analyzer_field(parameter, "name"),
                    alias.name
                )) {
                continue;
            }
            TSNode signature = script_callable_type_node(
                script_analyzer_field(parameter, "type")
            );
            TSNode inputs = script_analyzer_field(signature, "parameters");
            int ordinal = 0;
            for (uint32_t n = 0; n < script_analyzer_named_count(inputs); n++) {
                TSNode input = ts_node_named_child(inputs, n);
                if (ts_node_is_extra(input)) {
                    continue;
                }
                if (script_analyzer_text_is(a, script_analyzer_field(input, "name"), name)) {
                    return ordinal;
                }
                ordinal++;
            }
            return -1;
        }
    }
    return -2;
}
/* Effects become final only after the whole call graph has been analyzed. */
void script_analyzer_generic_check_callables(analyzer *a)
{
    for (int i = 0; i < acount(a->instances); i++) {
        const generic_instance *instance = &a->instances[i];
        if (instance->function < 0) {
            continue;
        }
        bool callable = false;
        bool suspended = false;
        for (int b = 0; b < acount(instance->bindings); b++) {
            compile_value binding = instance->bindings[b].value;
            callable |= binding.is_callable;
            if (!binding.is_callable) {
                continue;
            }
            const script_catalog_capability *
                capability = script_catalog_capability_by_id(a->catalog, binding.callable);
            suspended |= capability && (capability->effects & SCRIPT_EFFECT_SUSPEND);
            for (int f = 0; f < acount(a->out->functions); f++) {
                int symbol = a->out->functions[f].symbol;
                if (symbol >= 0 && a->out->symbols[symbol].id == binding.callable) {
                    suspended |= (a->out->functions[f].effects &
                                  SCRIPT_EFFECT_SUSPEND) != 0;
                }
            }
        }
        if (suspended) {
            script_analyzer_diagnostic(a, a->templates[instance->template].node, "Callable arguments must finish synchronously");
        }
        if (callable && (a->out->functions[instance->function].effects &
                         SCRIPT_EFFECT_SUSPEND)) {
            script_analyzer_diagnostic(
                a,
                a->templates[instance->template].node,
                "Functions with callable parameters must finish synchronously"
            );
        }
    }
}
bool script_analyzer_generic_member(analyzer *a, script_type type, const char *name, compile_value *out)
{
    for (int i = 0; i < acount(a->instances); i++) {
        if (a->instances[i].nominal == type.nominal && type.nominal) {
            int found = binding(a->instances[i].bindings, name);
            if (found >= 0) {
                *out = a->instances[i].bindings[found].value;
                return true;
            }
        }
    }
    for (int i = 0; i < a->catalog->specialization_count; i++) {
        const script_catalog_specialization *s = &a->catalog->specializations[i];
        if (!s->result.is_type || !type.nominal ||
            s->result.type.nominal != type.nominal) {
            continue;
        }
        for (int j = 0; j < s->binding_count; j++) {
            if (!strcmp(s->bindings[j].name, name)) {
                *out = s->bindings[j].value;
                return true;
            }
        }
    }
    return false;
}
static const char *own(analyzer *a, const char *text)
{
    char *owned = arena_alloc(&a->out->constants, strlen(text) + 1);
    strcpy(owned, text);
    return owned;
}
static const char *scalar_name(pin_type storage)
{
    return (storage >= PIN_BOOL && storage <= PIN_NAME) || storage == PIN_STR
             ? script_scalar_name(storage)
             : "value";
}
static char *specialization_name(analyzer *a, generic_template definition, compile_binding *bindings)
{
    char *name = xstrdup(a->out->symbols[definition.symbol].name);
    for (int i = 0; i < acount(bindings); i++) {
        compile_value value = bindings[i].value;
        char scalar[64];
        char *quoted = NULL;
        const char *spelling;
        if (value.is_type) {
            const script_catalog_type
                *type = script_catalog_type_by_id(a->catalog, value.type.nominal);
            spelling = type ? (type->source_name ? type->source_name : type->name)
                     : script_type_char(value.type)
                         ? "char"
                         : scalar_name(value.type.storage);
        } else if (behavior_scalar_string(value.value.type)) {
            pin_string text = pin_text(value.value);
            quoted = xmalloc((size_t)text.length * 6 + 3);
            char *at = quoted;
            *at++ = '"';
            for (const unsigned char *p = (const unsigned char *)text.bytes;
                 p < (const unsigned char *)text.bytes + text.length;
                 p++) {
                if (*p == '"' || *p == '\\') {
                    *at++ = '\\';
                    *at++ = (char)*p;
                } else if (*p == '\n') {
                    *at++ = '\\';
                    *at++ = 'n';
                } else if (*p == '\r') {
                    *at++ = '\\';
                    *at++ = 'r';
                } else if (*p == '\t') {
                    *at++ = '\\';
                    *at++ = 't';
                } else if (*p < 32 || *p >= 127) {
                    snprintf(at, 7, "\\u%04x", *p);
                    at += 6;
                } else {
                    *at++ = (char)*p;
                }
            }
            *at++ = '"';
            *at = 0;
            spelling = quoted;
        } else {
            if (value.value.type == PIN_BOOL) {
                snprintf(scalar, sizeof scalar, "%s", value.value.b ? "true" : "false");
            } else if (value.value.type == PIN_F32 || value.value.type == PIN_F64) {
                snprintf(
                    scalar,
                    sizeof scalar,
                    "%.17g",
                    value.value.type == PIN_F32 ? value.value.f : value.value.d
                );
            } else if (behavior_scalar_signed(value.value.type)) {
                snprintf(
                    scalar,
                    sizeof scalar,
                    "%lld",
                    (long long)(int64_t)behavior_scalar_bits(value.value)
                );
            } else {
                snprintf(
                    scalar,
                    sizeof scalar,
                    "%llu",
                    (unsigned long long)behavior_scalar_bits(value.value)
                );
            }
            spelling = scalar;
        }
        size_t old = strlen(name),
               size = old + strlen(bindings[i].name) + strlen(spelling) + 4;
        name = xrealloc(name, size);
        snprintf(name + old, size - old, "%c%s:%s", i ? ',' : '(', bindings[i].name, spelling);
        free(quoted);
    }
    size_t old = strlen(name);
    name = xrealloc(name, old + 2);
    strcpy(name + old, ")");
    return name;
}
static bool construct_record(analyzer *, int, compile_binding *, compile_value *, int *);
static const script_compile_binding *own_bindings(analyzer *, compile_binding *);

bool script_analyzer_generic_apply(
    analyzer *a,
    TSNode call,
    compile_binding *outer,
    compile_value *out,
    int *budget
)
{
    char *name = script_analyzer_qualified_name(a, script_analyzer_field(call, "function"));
    int symbol = script_analyzer_lookup_name(a, name);
    free(name);
    int template = template_symbol(a, symbol);
    if (template < 0 || !a->templates[template].record) {
        return false;
    }
    generic_template definition = a->templates[template];
    TSNode parameters = script_analyzer_field(definition.node, "parameters");
    TSNode arguments = script_analyzer_field(call, "arguments");
    compile_binding *bindings = NULL;
    bool valid = true;
    int ordinal = 0;
    for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
        TSNode argument = ts_node_named_child(arguments, i);
        if (ts_node_is_extra(argument)) {
            continue;
        }
        TSNode label = script_analyzer_field(argument, "name");
        TSNode parameter = ts_node_is_null(label)
                             ? parameter_at(parameters, ordinal++)
                             : parameter_named(a, parameters, label);
        TSNode value = ts_node_is_null(label)
                         ? argument
                         : script_analyzer_field(argument, "value");
        if (ts_node_is_null(value)) {
            value = argument;
        }
        compile_value result;
        TSNode parameter_type = script_analyzer_field(parameter, "type");
        script_type expected_argument = script_analyzer_text_is(a, parameter_type, "Type")
                                          ? (script_type){ 0 }
                                          : script_analyzer_type_node(a, parameter_type);
        bool ok = !ts_node_is_null(parameter) &&
                  script_analyzer_compile_as(a, value, outer, expected_argument.storage, &result, budget);
        if (ok) {
            TSNode type_node = script_analyzer_field(parameter, "type");
            if (script_analyzer_text_is(a, type_node, "Type")) {
                ok = result.is_type;
            } else {
                script_type expected = script_analyzer_type_node(a, type_node);
                ok = !result.is_type &&
                     (result.value.type == expected.storage ||
                      behavior_scalar_cast_supported(result.value.type, expected.storage));
                if (ok && result.value.type != expected.storage) {
                    behavior_scalar_cast(
                        result.value,
                        expected.storage,
                        BEHAVIOR_CAST_WRAP,
                        &result.value
                    );
                }
            }
        }
        if (!ok) {
            script_analyzer_diagnostic(a, argument, "Invalid compile-time type argument");
            valid = false;
            continue;
        }
        char *parameter_name = script_analyzer_name_copy(a, script_analyzer_field(parameter, "name"));
        if (binding(bindings, parameter_name) >= 0) {
            script_analyzer_diagnostic(a, argument, "Duplicate type argument '%s'", parameter_name);
            valid = false;
        } else {
            valid &= bind(a, argument, &bindings, parameter_name, result);
        }
        free(parameter_name);
    }
    int expected_count = 0;
    for (uint32_t i = 0; i < script_analyzer_named_count(parameters); i++) {
        expected_count += !ts_node_is_extra(ts_node_named_child(parameters, i));
    }
    if (acount(bindings) != expected_count) {
        script_analyzer_diagnostic(a, call, "Type specialization requires each constant parameter exactly once within the specialization budget");
        valid = false;
    }
    if (!valid) {
        free_bindings(bindings);
        return false;
    }
    asort(bindings, by_name);
    for (int i = 0; i < acount(a->instances); i++) {
        if (a->instances[i].template == template &&
            same_bindings(a->instances[i].bindings, bindings)) {
            *out = (compile_value){ .is_type = true,
                                    .type = { .storage = PIN_STRUCT,
                                              .nominal = a->instances[i].nominal } };
            free_bindings(bindings);
            return true;
        }
    }
    if (acount(a->instances) >= SPECIALIZATIONS) {
        script_analyzer_diagnostic(a, call, "Type specialization budget exceeded");
        free_bindings(bindings);
        return false;
    }
    return construct_record(a, template, bindings, out, budget);
}

static bool construct_record(
    analyzer *a,
    int template,
    compile_binding *bindings,
    compile_value *out,
    int *budget
)
{
    generic_template definition = a->templates[template];
    TSNode call = definition.node;
    bool valid = true;
    char *signature = specialization_name(a, definition, bindings);
    size_t size = strlen(a->out->name) + strlen(signature) + 2;
    char *identity = xmalloc(size);
    snprintf(identity, size, "%s.%s", a->out->name, signature);
    uint64_t id = mix(
        binding_id(a->out->symbols[definition.symbol].id, bindings, false),
        script_catalog_id("constructed-type", "")
    );
    if (!id) {
        id = 1;
    }
    script_catalog_field *fields = NULL;
    type_field *shape = NULL;
    bool deferred_layout = false;
    int old_scope = a->scope, old_function = a->function;
    a->scope = definition.scope;
    a->function = -1;
    a->scope = script_analyzer_scope_new(a, definition.node);
    install(a, definition.node, bindings);
    TSNode body = script_analyzer_field(definition.node, "body");
    for (uint32_t i = 0; i < script_analyzer_named_count(body); i++) {
        TSNode member = ts_node_named_child(body, i);
        if (ts_node_is_extra(member)) {
            continue;
        }
        char *field_name = script_analyzer_name_copy(a, script_analyzer_field(member, "name"));
        script_type type = script_analyzer_type_node(a, script_analyzer_field(member, "type"));
        const char *owned_name = own(a, field_name);
        free(field_name);
        type_field layout;
        valid &= script_analyzer_record_field(a, member, type, owned_name, &layout);
        const script_catalog_type *child = script_catalog_type_by_id(a->catalog, type.nominal);
        if (type.storage == PIN_STRUCT && child && child->authored_record) {
            if (!child->size || !child->layout) {
                /* A specialization in a field can precede its child's
                 * declaration. Resolve it after all fields are published. */
                deferred_layout = true;
            } else {
                const pin_record *record = script_analyzer_record_layout_for_type(a, type, member);
                layout.layout = record ? &record->type : NULL;
                valid &= record != NULL;
            }
        }
        apush(
            fields,
            ((script_catalog_field){ .id = script_catalog_id("record-field", owned_name),
                                     .name = owned_name,
                                     .type = type,
                                     .writable = true })
        );
        apush(shape, layout);
        TSNode default_node = script_analyzer_field(member, "value");
        if (!ts_node_is_null(default_node)) {
            compile_value value;
            bool ok = script_analyzer_compile_as(a, default_node, bindings, type.storage, &value, budget) &&
                      !value.is_type;
            if (ok && value.value.type != type.storage) {
                ok = behavior_scalar_cast_supported(value.value.type, type.storage);
                if (ok) {
                    behavior_scalar_cast(
                        value.value,
                        type.storage,
                        BEHAVIOR_CAST_WRAP,
                        &value.value
                    );
                }
            }
            if (!ok) {
                script_analyzer_diagnostic(a, default_node, "Generic record default requires a compatible constant value");
                valid = false;
            } else {
                alast(fields).has_default = true;
                alast(fields).default_value = value.value;
            }
        }
    }
    const pin_record *record = valid && !deferred_layout
                                 ? pin_record_create(&a->out->constants, identity, id, shape, acount(shape))
                                 : NULL;
    a->scope = old_scope;
    a->function = old_function;
    if (!valid || (!record && !deferred_layout)) {
        script_analyzer_diagnostic(a, call, "Constructed record requires supported fixed-layout fields");
        free_bindings(bindings);
        free(identity);
        free(signature);
        afree(fields);
        afree(shape);
        return false;
    }
    int count = acount(fields);
    script_catalog_field *owned_fields = arena_alloc(&a->out->constants, (size_t)count * sizeof *owned_fields);
    for (int i = 0; i < count; i++) {
        fields[i].offset = record ? record->type.fields[i].offset : 0;
        owned_fields[i] = fields[i];
    }
    script_type type = { .storage = PIN_STRUCT, .nominal = id };
    script_analyzer_publish_type(
        a,
        (script_catalog_type){ .id = id,
                               .name = own(a, identity),
                               .source_name = own(a, signature),
                               .type = type,
                               .fields = owned_fields,
                               .field_count = count,
                               .size = record ? record->type.size : 0,
                               .layout = record ? record->layout : 0,
                               .authored_record = true,
                               .error = script_analyzer_error_attribute(a, definition.node) }
    );
    apush(
        a->instances,
        ((generic_instance){ .template = template, .function = -1, .nominal = id, .bindings = bindings })
    );
    *out = (compile_value){ .is_type = true, .type = type };
    apush(
        a->out->specializations,
        ((script_catalog_specialization){
            .id = binding_id(a->out->symbols[definition.symbol].id, bindings, false),
            .template_id = a->out->symbols[definition.symbol].id,
            .bindings = own_bindings(a, bindings),
            .binding_count = acount(bindings),
            .result = *out })
    );
    a->out->specialization_count = acount(a->out->specializations);
    free(identity);
    free(signature);
    afree(fields);
    afree(shape);
    return true;
}
script_type script_analyzer_generic_type(analyzer *a, TSNode node)
{
    compile_value value;
    int budget = COMPILE_STEPS;
    if (!script_analyzer_compile_expression(a, node, NULL, &value, &budget) ||
        !value.is_type) {
        script_analyzer_diagnostic(a, node, "Type expression must construct a compile-time type");
        return script_analyzer_bad_type();
    }
    return value.type;
}
bool script_analyzer_generic_alias(analyzer *a, TSNode node)
{
    TSNode expression = script_analyzer_field(node, "value");
    if (!script_analyzer_is(expression, "call_expression")) {
        return false;
    }
    char *name = script_analyzer_qualified_name(a, script_analyzer_field(expression, "function"));
    int symbol = script_analyzer_lookup_name(a, name);
    if (symbol < 0) {
        symbol = script_analyzer_callable_symbol(a, name);
    }
    bool imported = false;
    for (int i = 0; i < a->catalog->template_count; i++) {
        if (!strcmp(a->catalog->templates[i].name, name)) {
            imported = true;
        }
    }
    free(name);
    if (!imported &&
        (symbol < 0 || (template_symbol(a, symbol) < 0 &&
                        a->out->symbols[symbol].kind != SCRIPT_FUNCTION))) {
        return false;
    }
    compile_value value;
    int budget = COMPILE_STEPS;
    if (!script_analyzer_compile_expression(a, expression, NULL, &value, &budget)) {
        return true;
    }
    /* The value keeps its storage; a char result keeps its declared type. */
    script_type type = { .storage = value.value.type };
    int function = !imported && symbol >= 0 ? a->out->symbols[symbol].function : -1;
    if (function >= 0 && acount(a->out->functions[function].results) == 1) {
        script_type
            declared = a->out->symbols[a->out->functions[function].results[0]].type;
        if (script_type_char(declared) && declared.storage == type.storage) {
            type = declared;
        }
    }
    int alias = script_analyzer_symbol_new(
        a,
        script_analyzer_field(node, "name"),
        value.is_type ? SCRIPT_TYPE : SCRIPT_CONSTANT,
        value.is_type ? value.type : type,
        false
    );
    if (!value.is_type) {
        a->out->symbols[alias].initializer = script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                 .type = type,
                                 .constant = value.value,
                                 .constant_snapshot = true,
                                 .range = script_analyzer_range(a, expression),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1 }
        );
    }
    return true;
}
void script_analyzer_generic_export(analyzer *a, TSNode node, int symbol)
{
    if (a->function >= 0) {
        return; /* Lexically nested callables are not library exports. */
    }
    uint64_t id = a->out->symbols[symbol].id;
    for (int i = 0; i < acount(a->out->templates); i++) {
        if (a->out->templates[i].id == id) {
            return;
        }
    }
    bool record = script_analyzer_is(node, "record_declaration");
    script_template_parameter *parameters = NULL;
    TSNode signature = script_analyzer_field(node, "parameters");
    for (uint32_t i = 0; i < script_analyzer_named_count(signature); i++) {
        TSNode parameter = ts_node_named_child(signature, i);
        if (ts_node_is_extra(parameter)) {
            continue;
        }
        TSNode target = scoped_target(a, parameter);
        TSNode type_node = ts_node_is_null(target)
                             ? script_analyzer_field(parameter, "type")
                             : target;
        TSNode callable_node = script_callable_type_node(type_node);
        TSNode type_binder = ts_node_is_null(callable_node) ? binder(type_node)
                                                            : (TSNode){ 0 };
        TSNode name_node = ts_node_is_null(type_binder)
                             ? script_analyzer_field(type_node, "name")
                             : script_analyzer_field(type_binder, "name");
        char *name = script_analyzer_name_copy(a, script_analyzer_field(parameter, "name"));
        char *spelling = ts_node_is_null(name_node)
                           ? xstrdup("")
                           : script_analyzer_name_copy(a, name_node);
        bool type_value = !strcmp(spelling, "Type");
        script_type type = type_value ? (script_type){ .storage = PIN_FLOW }
                                      : script_builtin_type(spelling);
        const char *bind_name = NULL;
        if (!type_value && (type.flags & SCRIPT_TYPE_ERROR)) {
            int index = script_catalog_find_type(a->catalog, spelling);
            if (index >= 0) {
                type = a->catalog->types[index].type;
                type.nominal = a->catalog->types[index].id;
            } else {
                type = (script_type){ .flags = SCRIPT_TYPE_UNTYPED };
                bind_name = own(a, spelling);
            }
        }
        if (!ts_node_is_null(target)) {
            type.flags |= SCRIPT_TYPE_REFERENCE;
        }
        if (outer_optional(a, type_node)) {
            type.flags |= SCRIPT_TYPE_OPTIONAL;
        }
        if (script_analyzer_token_present(a, type_node, "[") ||
            script_analyzer_token_present(a, type_node, "[]")) {
            type.container = script_analyzer_token_present(a, type_node, "view")
                               ? PIN_CONTAINER_VIEW
                               : PIN_CONTAINER_ARRAY;
        }
        char *callable_text = ts_node_is_null(callable_node)
                                ? NULL
                                : script_analyzer_text_copy(a, callable_node);
        apush(
            parameters,
            ((script_template_parameter){
                .name = own(a, name),
                .binder = bind_name,
                .type = type,
                .type_value = type_value,
                .comptime = record ||
                            !ts_node_is_null(script_analyzer_field(parameter, "comptime")),
                .binds_type = !ts_node_is_null(type_binder),
                .callable_type = callable_text ? own(a, callable_text) : NULL })
        );
        free(callable_text);
        free(name);
        free(spelling);
    }
    int count = acount(parameters);
    script_template_parameter *owned = count
                                         ? arena_alloc(&a->out->constants, (size_t)count * sizeof *owned)
                                         : NULL;
    if (count) {
        memcpy(owned, parameters, (size_t)count * sizeof *owned);
    }
    char *text = script_analyzer_text_copy(a, node);
    script_range range = script_analyzer_range(a, node);
    apush(
        a->out->templates,
        ((script_catalog_template){
            .id = id,
            .name = own(a, a->out->symbols[symbol].name),
            .owner = own(a, a->out->identity),
            .kind = record               ? SCRIPT_TEMPLATE_RECORD
                  : type_result(a, node) ? SCRIPT_TEMPLATE_TYPE_FUNCTION
                                         : SCRIPT_TEMPLATE_FUNCTION,
            .source_generation = script_catalog_id("template-source", text),
            .parameters = owned,
            .parameter_count = count,
            .start = range.start,
            .end = range.end,
            .generic = record || generic(a, node) })
    );
    a->out->template_count = acount(a->out->templates);
    free(text);
    afree(parameters);
}
static compile_binding *copy_bindings(analyzer *a, const script_compile_binding *source, int count)
{
    compile_binding *bindings = NULL;
    for (int i = 0; i < count; i++) {
        compile_binding copy = { xstrdup(source[i].name), source[i].value };
        if (!copy.value.is_type && !copy.value.is_callable) {
            pin_value value;
            int error = pin_value_capture_to_arena(
                &a->out->constants,
                &value,
                &copy.value.value
            );
            assert(!error);
            (void)error;
            copy.value.value = value;
        }
        apush(bindings, copy);
    }
    asort(bindings, by_name);
    return bindings;
}
static const script_compile_binding *own_bindings(analyzer *a, compile_binding *source)
{
    int count = acount(source);
    script_compile_binding *owned = count
                                      ? arena_alloc(&a->out->constants, (size_t)count * sizeof *owned)
                                      : NULL;
    for (int i = 0; i < count; i++) {
        owned[i] = source[i];
        owned[i].name = (char *)own(a, source[i].name);
        if (!owned[i].value.is_type && !owned[i].value.is_callable) {
            pin_value value;
            int error = pin_value_capture_to_arena(
                &a->out->constants,
                &value,
                &owned[i].value.value
            );
            assert(!error);
            (void)error;
            owned[i].value.value = value;
        }
    }
    return owned;
}
static const script_catalog_template *import_template(analyzer *a, TSNode node, const char *name)
{
    if (a->document_count) {
        return script_analyzer_package_template(a, node, name);
    }
    for (int i = 0; i < a->catalog->template_count; i++) {
        const script_catalog_template *t = &a->catalog->templates[i];
        if (!strcmp(t->name, name) && strcmp(t->owner, a->out->identity)) {
            return t;
        }
    }
    return NULL;
}
static const script_catalog_specialization *import_request(
    analyzer *a,
    TSNode call,
    const script_catalog_template *t,
    compile_binding *outer,
    bool evaluate,
    int **resolved,
    int *budget
)
{
    TSNode arguments = script_analyzer_field(call, "arguments");
    int count = (int)script_analyzer_named_count(arguments), ordinal = 0;
    int *values = resolved ? xmalloc((size_t)(count + 1) * sizeof *values) : NULL;
    bool *seen = xcalloc((size_t)(t->parameter_count + 1), sizeof *seen);
    compile_binding *bindings = NULL;
    bool valid = true;
    for (int i = 0; i < count; i++) {
        if (values) {
            values[i] = -1;
        }
        TSNode argument = ts_node_named_child(arguments, (uint32_t)i);
        if (ts_node_is_extra(argument)) {
            continue;
        }
        TSNode label = script_analyzer_field(argument, "name");
        int p = ordinal++;
        if (!ts_node_is_null(label)) {
            char *name = script_analyzer_name_copy(a, label);
            p = -1;
            for (int j = 0; j < t->parameter_count; j++) {
                if (!strcmp(name, t->parameters[j].name)) {
                    p = j;
                }
            }
            free(name);
        }
        if (p < 0 || p >= t->parameter_count || seen[p]) {
            script_analyzer_diagnostic(a, argument, "Unknown or repeated polymorphic argument");
            valid = false;
            continue;
        }
        seen[p] = true;
        const script_template_parameter *parameter = &t->parameters[p];
        TSNode value = ts_node_is_null(label)
                         ? argument
                         : script_analyzer_field(argument, "value");
        if (ts_node_is_null(value)) {
            value = argument;
        }
        if (evaluate || parameter->comptime || parameter->type_value) {
            compile_value constant = { 0 };
            bool ok = script_analyzer_compile_as(
                a,
                value,
                outer,
                parameter->type.storage,
                &constant,
                budget
            );
            if (ok && constant.is_type && (constant.type.flags & SCRIPT_TYPE_ERROR)) {
                ok = false;
            }
            if (ok && parameter->type_value != constant.is_type) {
                script_analyzer_diagnostic(
                    a,
                    value,
                    "Compile-time parameter requires %s",
                    parameter->type_value ? "a type" : "a value"
                );
                ok = false;
            }
            if (ok && !constant.is_type &&
                !(parameter->type.flags & SCRIPT_TYPE_UNTYPED) &&
                constant.value.type != parameter->type.storage) {
                if (!behavior_scalar_cast_supported(
                        constant.value.type,
                        parameter->type.storage
                    )) {
                    ok = false;
                } else {
                    behavior_scalar_cast(
                        constant.value,
                        parameter->type.storage,
                        BEHAVIOR_CAST_WRAP,
                        &constant.value
                    );
                }
            }
            valid &= ok && bind(a, value, &bindings, parameter->name, constant);
            if (values) {
                values[i] = -2;
            }
            if (ok && parameter->binder && !constant.is_type) {
                valid &= bind(
                    a,
                    value,
                    &bindings,
                    parameter->binder,
                    (compile_value){ .is_type = true,
                                     .type = { .storage = constant.value.type } }
                );
            }
        } else {
            script_type expected = parameter->type;
            if (parameter->binder) {
                expected = (script_type){ .flags = SCRIPT_TYPE_UNTYPED };
            }
            values[i] = parameter->type.flags & SCRIPT_TYPE_REFERENCE
                          ? script_analyzer_scoped_argument(a, value, expected)
                          : script_analyzer_expression(a, value, expected);
            script_type actual = concrete(script_analyzer_expr_type(a, values[i]));
            if (actual.flags & SCRIPT_TYPE_ERROR) {
                valid = false;
                continue;
            }
            actual = binding_type(actual, parameter->type.flags & SCRIPT_TYPE_OPTIONAL);
            if (parameter->type.container == PIN_CONTAINER_ARRAY ||
                parameter->type.container == PIN_CONTAINER_VIEW) {
                if (actual.container != parameter->type.container) {
                    valid = false;
                    script_analyzer_diagnostic(
                        a,
                        value,
                        parameter->type.container == PIN_CONTAINER_VIEW
                            ? "Polymorphic view argument requires a view"
                            : "Polymorphic array argument requires an array"
                    );
                } else {
                    actual = script_catalog_element_type(a->catalog, actual);
                }
            }
            if (parameter->binder) {
                valid &= bind(
                    a,
                    value,
                    &bindings,
                    parameter->binder,
                    (compile_value){ .is_type = true, .type = actual }
                );
            }
        }
    }
    for (int i = 0; i < t->parameter_count; i++) {
        if (!seen[i]) {
            valid = false;
            script_analyzer_diagnostic(a, call, "Polymorphic call requires each argument exactly once");
        }
    }
    free(seen);
    asort(bindings, by_name);
    if (resolved) {
        *resolved = values;
    }
    if (!valid) {
        free_bindings(bindings);
        return NULL;
    }
    uint64_t id = binding_id(t->id, bindings, evaluate);
    bool recorded = false;
    for (int i = 0; i < acount(a->out->requests); i++) {
        if (a->out->requests[i].id == id) {
            const script_specialization_request *previous = &a->out->requests[i];
            bool equal = previous->template_id == t->id &&
                         previous->evaluate == evaluate &&
                         previous->binding_count == acount(bindings);
            for (int j = 0; equal && j < acount(bindings); j++) {
                equal = !strcmp(previous->bindings[j].name, bindings[j].name) &&
                        equal_value(previous->bindings[j].value, bindings[j].value);
            }
            if (!equal) {
                script_analyzer_diagnostic(a, call, "Specialization identity collision between distinct binding tuples");
                free_bindings(bindings);
                return NULL;
            }
            recorded = true;
        }
    }
    if (!recorded) {
        apush(
            a->out->requests,
            ((script_specialization_request){ .id = id,
                                              .template_id = t->id,
                                              .owner = own(a, t->owner),
                                              .bindings = own_bindings(a, bindings),
                                              .binding_count = acount(bindings),
                                              .origin = script_analyzer_range(a, call),
                                              .evaluate = evaluate })
        );
        a->out->request_count = acount(a->out->requests);
    }
    const script_catalog_specialization *result = NULL;
    for (int i = 0; i < a->catalog->specialization_count; i++) {
        const script_catalog_specialization *candidate = &a->catalog->specializations[i];
        if (candidate->id != id || candidate->template_id != t->id ||
            candidate->binding_count != acount(bindings)) {
            continue;
        }
        bool equal = true;
        for (int j = 0; j < acount(bindings); j++) {
            equal &= !strcmp(candidate->bindings[j].name, bindings[j].name) &&
                     equal_value(candidate->bindings[j].value, bindings[j].value);
        }
        if (equal) {
            result = candidate;
        }
    }
    free_bindings(bindings);
    if (!result) {
        a->pending_specializations++;
    }
    return result;
}
bool script_analyzer_generic_import_call(
    analyzer *a,
    TSNode call,
    const char *name,
    int *expression,
    const script_catalog_capability **capability,
    int **arguments
)
{
    const script_catalog_template *t = import_template(a, call, name);
    if (!t || !t->generic || t->kind != SCRIPT_TEMPLATE_FUNCTION) {
        return false;
    }
    int budget = COMPILE_STEPS;
    const script_catalog_specialization
        *s = import_request(a, call, t, NULL, false, arguments, &budget);
    *capability = s ? script_catalog_capability_by_id(a->catalog, s->id) : NULL;
    *expression = -1;
    if (!*capability) {
        *expression = script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                 .type = script_analyzer_bad_type(),
                                 .range = script_analyzer_range(a, call),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1 }
        );
    }
    return true;
}
bool script_analyzer_generic_import_compile(
    analyzer *a,
    TSNode call,
    compile_binding *outer,
    compile_value *out,
    int *budget
)
{
    char *name = script_analyzer_qualified_name(a, script_analyzer_field(call, "function"));
    const script_catalog_template *t = import_template(a, call, name);
    free(name);
    if (!t) {
        return false;
    }
    const script_catalog_specialization *
        s = import_request(a, call, t, outer, t->kind != SCRIPT_TEMPLATE_RECORD, NULL, budget);
    *out = s ? s->result
             : (compile_value){ .is_type = true, .type = script_analyzer_bad_type() };
    return true;
}
void script_analyzer_generic_materialize(analyzer *a)
{
    for (int i = 0; i < a->requested_count; i++) {
        const script_specialization_request *request = &a->requested[i];
        if (strcmp(request->owner, a->out->identity)) {
            continue;
        }
        int template = -1;
        for (int j = 0; j < acount(a->templates); j++) {
            if (a->out->symbols[a->templates[j].symbol].id == request->template_id) {
                template = j;
                break;
            }
        }
        const script_catalog_template *descriptor = NULL;
        for (int j = 0; j < acount(a->out->templates); j++) {
            if (a->out->templates[j].id == request->template_id) {
                descriptor = &a->out->templates[j];
                break;
            }
        }
        if (!descriptor) {
            script_analyzer_diagnostic(a, ts_tree_root_node(a->syntax->tree), "Unknown owned specialization template");
            continue;
        }
        compile_binding *bindings = copy_bindings(a, request->bindings, request->binding_count);
        const script_compile_binding *published = own_bindings(a, bindings);
        compile_value result = { 0 };
        bool executable = !request->evaluate && descriptor->generic &&
                          descriptor->kind == SCRIPT_TEMPLATE_FUNCTION;
        bool valid = true;
        int pending = a->pending_specializations;
        int diagnostics = acount(a->out->diagnostics);
        if (executable && template >= 0) {
            int function = -1;
            for (int j = 0; j < acount(a->instances); j++) {
                if (a->instances[j].template == template &&
                    same_bindings(a->instances[j].bindings, bindings)) {
                    function = a->instances[j].function;
                    break;
                }
            }
            if (function >= 0) {
                free_bindings(bindings);
            } else {
                function = instantiate(a, template, bindings);
            }
            bindings = NULL;
            int symbol = a->out->functions[function].symbol;
            if (a->out->symbols[symbol].id != request->id) {
                script_analyzer_diagnostic(a, a->templates[template].node, "Specialization identity differs from its canonical binding tuple");
                valid = false;
            }
        } else if (descriptor->kind == SCRIPT_TEMPLATE_RECORD && template >= 0) {
            uint64_t nominal = 0;
            for (int j = 0; j < acount(a->instances); j++) {
                if (a->instances[j].template == template &&
                    same_bindings(a->instances[j].bindings, bindings)) {
                    nominal = a->instances[j].nominal;
                    break;
                }
            }
            if (nominal) {
                result = (compile_value){ .is_type = true,
                                          .type = { .storage = PIN_STRUCT,
                                                    .nominal = nominal } };
                free_bindings(bindings);
            } else {
                int budget = COMPILE_STEPS;
                valid = construct_record(a, template, bindings, &result, &budget);
            }
            bindings = NULL;
        } else {
            TSNode declaration = { 0 };
            int scope = a->scope;
            if (template >= 0) {
                declaration = a->templates[template].node;
                scope = a->templates[template].scope;
            } else {
                for (int j = 0; j < acount(a->bodies); j++) {
                    int function = a->bodies[j].function;
                    int symbol = a->out->functions[function].symbol;
                    if (symbol >= 0 &&
                        a->out->symbols[symbol].id == request->template_id) {
                        declaration = ts_node_parent(a->bodies[j].node);
                        scope = a->out->functions[function].scope;
                        break;
                    }
                }
            }
            int budget = COMPILE_STEPS;
            valid = !ts_node_is_null(declaration) &&
                    script_analyzer_compile_procedure(a, declaration, scope, bindings, &result, &budget);
        }
        free_bindings(bindings);
        bool published_before = false;
        for (int j = 0; j < acount(a->out->specializations); j++) {
            published_before |= a->out->specializations[j].id == request->id;
        }
        if (valid && !published_before && pending == a->pending_specializations &&
            diagnostics == acount(a->out->diagnostics)) {
            apush(
                a->out->specializations,
                ((script_catalog_specialization){ .id = request->id,
                                                  .template_id = request->template_id,
                                                  .bindings = published,
                                                  .binding_count = request->binding_count,
                                                  .result = result,
                                                  .executable = executable })
            );
        }
    }
    a->out->specialization_count = acount(a->out->specializations);
}
void script_analyzer_generic_free(analyzer *a)
{
    for (int i = 0; i < acount(a->instances); i++) {
        free_bindings(a->instances[i].bindings);
        afree(a->instances[i].constant_arguments);
    }
    afree(a->instances);
    afree(a->templates);
}
