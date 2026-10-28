#include "language/callable_headers.h"

#include "support/array.h"
#include "yyjson.h"

#include <stdio.h>
#include <string.h>

static bool is(TSNode node, const char *kind)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), kind);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_is_null(node)
             ? (TSNode){ 0 }
             : ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static uint32_t named_count(TSNode node)
{
    return ts_node_is_null(node) ? 0 : ts_node_named_child_count(node);
}
static script_range node_range(TSNode node, int document)
{
    if (ts_node_is_null(node)) {
        return (script_range){ .line = 1, .column = 1 };
    }
    TSPoint p = ts_node_start_point(node);
    return (script_range){ .start = (int)ts_node_start_byte(node),
                           .end = (int)ts_node_end_byte(node),
                           .line = (int)p.row + 1,
                           .column = (int)p.column + 1,
                           .document = document };
}
static void *copy(arena *storage, const void *input, size_t bytes)
{
    if (!bytes) {
        return NULL;
    }
    void *out = arena_alloc(storage, bytes);
    memcpy(out, input, bytes);
    return out;
}
static char *own(arena *storage, const char *text)
{
    return text ? copy(storage, text, strlen(text) + 1) : NULL;
}
script_function script_callable_function(int parent, TSNode node, int document, script_function_kind kind, int symbol)
{
    return (script_function){ .kind = kind,
                              .symbol = symbol,
                              .scope = -1,
                              .body = -1,
                              .receiver = -1,
                              .parent = parent,
                              .range = node_range(node, document),
                              .depth_limit = SCRIPT_CALL_DEPTH_LIMIT };
}
script_type script_callable_result_type(uint64_t function, const script_type *results, int count)
{
    return count == 1 ? results[0]
                      : (script_type){ .storage = count ? PIN_STRUCT : PIN_FLOW,
                                       .nominal = count ? function : 0,
                                       .flags = count ? SCRIPT_TYPE_RESULTS : 0 };
}
typedef struct
{
    arena *storage;
    script_signature_facts facts;
    script_catalog catalog;
    script_symbol *symbols;
    script_scope *scopes;
    script_function *functions;
    script_catalog_type *types, *own_types;
    script_use *uses;
    unsigned *changes;
    bool *function_changes;
    script_diagnostic *diagnostics;
    script_callable_initializer *initializers;
    script_callable_body *bodies;
    const char *source;
    script_callable_context context;
    int initial_symbols, initial_scopes, initial_functions, initial_uses, initial_own;
} headers;
static void synchronize(headers *h)
{
    h->catalog.types = h->types;
    h->catalog.type_count = acount(h->types);
    h->facts.named.catalog = &h->catalog;
    h->facts.named.own_types = h->own_types;
    h->facts.named.own_type_count = acount(h->own_types);
    h->facts.named.uses = h->uses;
    h->facts.named.use_count = acount(h->uses);
    h->facts.named.lexical.symbols = h->symbols;
    h->facts.named.lexical.symbol_count = acount(h->symbols);
    h->facts.named.lexical.scopes = h->scopes;
    h->facts.named.lexical.functions = h->functions;
    h->facts.functions = h->functions;
    h->facts.function_count = acount(h->functions);
}
static headers begin(arena *storage, const script_callable_facts *facts, const char *source, script_callable_context context)
{
    headers h = { .storage = storage,
                  .facts = facts->signature,
                  .catalog = *facts->signature.named.catalog,
                  .source = source,
                  .context = context,
                  .initial_symbols = facts->signature.named.lexical.symbol_count,
                  .initial_scopes = facts->scope_count,
                  .initial_functions = facts->signature.function_count,
                  .initial_uses = facts->signature.named.use_count,
                  .initial_own = facts->signature.named.own_type_count };
    for (int i = 0; i < h.initial_symbols; i++) {
        apush(h.symbols, facts->signature.named.lexical.symbols[i]);
        apush(h.changes, 0);
    }
    for (int i = 0; i < h.initial_scopes; i++) {
        apush(h.scopes, facts->signature.named.lexical.scopes[i]);
    }
    for (int i = 0; i < h.initial_functions; i++) {
        script_function f = facts->signature.functions[i];
        f.parameters = NULL;
        f.results = NULL;
        const script_function *original = &facts->signature.functions[i];
        for (int j = 0; j < acount(original->parameters); j++) {
            apush(f.parameters, original->parameters[j]);
        }
        for (int j = 0; j < acount(original->results); j++) {
            apush(f.results, original->results[j]);
        }
        apush(h.functions, f);
        apush(h.function_changes, false);
    }
    for (int i = 0; i < h.catalog.type_count; i++) {
        apush(h.types, h.catalog.types[i]);
    }
    for (int i = 0; i < h.initial_own; i++) {
        apush(h.own_types, facts->signature.named.own_types[i]);
    }
    for (int i = 0; i < h.initial_uses; i++) {
        apush(h.uses, facts->signature.named.uses[i]);
    }
    return h;
}
static void diagnostic(headers *h, TSNode node, const char *message)
{
    script_diagnostic d = { .range = node_range(node, h->context.document) };
    snprintf(d.message, sizeof d.message, "%s", message);
    apush(h->diagnostics, d);
}
static char *node_name(headers *h, TSNode node)
{
    size_t start = ts_node_is_null(node) ? 0 : ts_node_start_byte(node);
    size_t end = ts_node_is_null(node) ? 0 : ts_node_end_byte(node);
    char *text = arena_alloc(h->storage, end - start + 1);
    memcpy(text, h->source + start, end - start);
    text[end - start] = 0;
    if (*text != '"') {
        return text;
    }
    yyjson_doc *doc = yyjson_read(text, end - start, 0);
    yyjson_val *value = doc ? yyjson_doc_get_root(doc) : NULL;
    if (!yyjson_is_str(value) ||
        yyjson_get_len(value) != strlen(yyjson_get_str(value))) {
        diagnostic(h, node, "Invalid string or embedded NUL");
        yyjson_doc_free(doc);
        return own(h->storage, "");
    }
    char *decoded = own(h->storage, yyjson_get_str(value));
    yyjson_doc_free(doc);
    return decoded;
}
static int declare(
    headers *h,
    const char *name,
    script_range range,
    script_symbol_kind kind,
    script_type type,
    bool writable
)
{
    synchronize(h);
    script_lexical_declaration d = script_lexical_declare(
        &h->facts.named.lexical,
        (script_lexical_context){ h->context.scope,
                                  h->context.function,
                                  h->facts.named.package_scope,
                                  h->facts.named.document_count,
                                  h->context.specialization },
        range,
        name,
        kind
    );
    for (int i = 0; i < d.diagnostic_count; i++) {
        apush(h->diagnostics, d.diagnostics[i]);
    }
    if (d.existing >= 0) {
        return d.existing;
    }
    script_symbol symbol = { .id = d.id,
                             .name = own(h->storage, name),
                             .kind = kind,
                             .type = type,
                             .declaration = range,
                             .scope = h->context.scope,
                             .initializer = -1,
                             .function = -1,
                             .writable = writable };
    apush(h->symbols, symbol);
    apush(h->changes, SCRIPT_CALLABLE_APPEND);
    return acount(h->symbols) - 1;
}
static int declare_node(headers *h, TSNode node, script_symbol_kind kind, script_type type, bool writable)
{
    return declare(h, node_name(h, node), node_range(node, h->context.document), kind, type, writable);
}
static script_signature_reading annotation(headers *h, TSNode node, bool signature)
{
    synchronize(h);
    script_signature_reading read = script_signature_read(
        h->storage,
        &h->facts,
        node,
        h->source,
        h->context.scope,
        h->context.document,
        signature
    );
    for (int i = 0; i < read.diagnostic_count; i++) {
        apush(h->diagnostics, read.diagnostics[i]);
    }
    for (int i = 0; i < read.use_count; i++) {
        apush(h->uses, read.uses[i]);
    }
    for (int i = 0; i < read.type_count; i++) {
        apush(h->types, read.types[i]);
        apush(h->own_types, read.types[i]);
    }
    return read;
}
static int function_new(headers *h, TSNode node, script_function_kind kind, int symbol)
{
    script_function
        f = script_callable_function(h->context.function, node, h->context.document, kind, symbol);
    apush(h->functions, f);
    apush(h->function_changes, true);
    return acount(h->functions) - 1;
}
static void set_unit(headers *h, int symbol, const char *unit)
{
    h->symbols[symbol].unit = own(h->storage, unit);
    h->changes[symbol] |= SCRIPT_CALLABLE_UNIT;
}
/* A pointer parameter whose callers' addresses never outlive the call: one
 * that is `^T`, not nullable, and whose body only dereferences it, reads
 * its target's members or passes it on to another such parameter. It
 * aliases the caller's location, so it can also accept an engine-owned
 * address; any other use makes it an ordinary managed pointer. */
enum
{
    SCOPED_POINTER_DEPTH = 64
};
typedef struct
{
    uint32_t declaration, parameter;
} scoped_pointer_visit;
/* Whether two nodes have the same spelling. */
static bool same_text(const char *source, TSNode a, TSNode b)
{
    uint32_t length = ts_node_end_byte(a) - ts_node_start_byte(a);
    return !ts_node_is_null(a) && !ts_node_is_null(b) &&
           length == ts_node_end_byte(b) - ts_node_start_byte(b) &&
           !memcmp(source + ts_node_start_byte(a), source + ts_node_start_byte(b), length);
}
/* The `^T`, possibly measured, that a parameter annotation spells, or a
 * null node when it adds `?`, `[]` or a trait. */
static TSNode pointer_annotation(TSNode type)
{
    if (is(type, "measured_type")) {
        type = field(type, "base");
    }
    if (!is(type, "type") || named_count(type) != 1 ||
        !is(ts_node_named_child(type, 0), "pointer_type")) {
        return (TSNode){ 0 };
    }
    for (uint32_t i = 0; i < ts_node_child_count(type); i++) {
        if (!ts_node_is_named(ts_node_child(type, i))) {
            return (TSNode){ 0 };
        }
    }
    return ts_node_named_child(type, 0);
}
static bool callable_declaration(TSNode node)
{
    return is(node, "function_declaration") || is(node, "macro_declaration");
}
/* The declaration a plain call names: the nearest callable declared in a
 * block around the call, nested in its caller or beside it. */
static TSNode visible_callable(const char *source, TSNode call)
{
    TSNode callee = field(call, "function");
    for (TSNode scope = ts_node_parent(call);
         is(callee, "identifier") && !ts_node_is_null(scope);
         scope = ts_node_parent(scope)) {
        for (uint32_t i = 0; i < named_count(scope); i++) {
            TSNode each = ts_node_named_child(scope, i);
            if (callable_declaration(each) &&
                same_text(source, callee, field(each, "name"))) {
                return each;
            }
        }
    }
    return (TSNode){ 0 };
}
static bool scoped_pointer(
    const char *source,
    TSNode declaration,
    uint32_t index,
    scoped_pointer_visit *stack,
    int depth
);
/* Whether `argument`, a use of the parameter, is passed to a scoped pointer
 * parameter of a callable the call can see. */
static bool scoped_forward(const char *source, TSNode argument, scoped_pointer_visit *stack, int depth)
{
    TSNode list = ts_node_parent(argument), label = { 0 };
    while (is(list, "parenthesized_expression")) {
        argument = list;
        list = ts_node_parent(list);
    }
    if (is(list, "named_argument") ||
        (is(list, "typed_argument") && ts_node_eq(field(list, "value"), argument))) {
        label = field(list, "name");
        argument = list;
        list = ts_node_parent(list);
    }
    TSNode call = ts_node_parent(list);
    if (!is(list, "argument_list") || !is(call, "call_expression")) {
        return false;
    }
    TSNode callee = visible_callable(source, call);
    TSNode parameters = field(callee, "parameters");
    for (uint32_t i = 0, position = 0; i < named_count(list); i++) {
        TSNode current = ts_node_named_child(list, i);
        if (!ts_node_eq(current, argument)) {
            position += !ts_node_is_extra(current) &&
                        !is(current, "named_argument") &&
                        !is(current, "typed_argument");
            continue;
        }
        for (uint32_t j = 0, slot = 0; j < named_count(parameters); j++) {
            TSNode parameter = ts_node_named_child(parameters, j);
            if (ts_node_is_extra(parameter)) {
                continue;
            }
            bool match = ts_node_is_null(label)
                           ? slot++ == position
                           : same_text(source, label, field(parameter, "name"));
            if (match) {
                return !ts_node_is_null(pointer_annotation(field(parameter, "type"))) &&
                       scoped_pointer(source, callee, j, stack, depth);
            }
        }
        return false;
    }
    return false;
}
/* Whether `node` spells a name in `parent` rather than reading a value. */
static bool name_position(TSNode parent, TSNode node)
{
    return is(parent, "argument_name") || is(parent, "type_name") ||
           ts_node_eq(field(parent, "name"), node) ||
           ts_node_eq(field(parent, "member"), node) ||
           (is(parent, "for_statement") && ts_node_eq(field(parent, "key"), node));
}
static bool binding_form(TSNode node)
{
    return is(node, "local_declaration") || is(node, "for_statement") ||
           is(node, "query_generator");
}
/* Whether every use of `name` in `node` keeps its address inside the call. */
static bool scoped_uses(const char *source, TSNode node, TSNode name, scoped_pointer_visit *stack, int depth)
{
    if (callable_declaration(node)) {
        return true; /* Nested callables cannot see enclosing locals. */
    }
    if (!is(node, "identifier") || !same_text(source, node, name)) {
        for (uint32_t i = 0; i < named_count(node); i++) {
            if (!scoped_uses(source, ts_node_named_child(node, i), name, stack, depth)) {
                return false;
            }
        }
        return true;
    }
    TSNode parent = ts_node_parent(node);
    if (name_position(parent, node)) {
        return !binding_form(parent); /* Shadowing keeps the analysis simple. */
    }
    if (!is(parent, "dereference_expression") && !is(parent, "member_expression")) {
        return scoped_forward(source, node, stack, depth);
    }
    TSNode place = parent;
    while (is(ts_node_parent(place), "dereference_expression") ||
           is(ts_node_parent(place), "parenthesized_expression") ||
           (is(ts_node_parent(place), "member_expression") &&
            ts_node_eq(field(ts_node_parent(place), "object"), place))) {
        place = ts_node_parent(place);
    }
    TSNode outer = ts_node_parent(place);
    return !is(outer, "address_expression") ||
           scoped_forward(source, outer, stack, depth);
}
static bool scoped_pointer(
    const char *source,
    TSNode declaration,
    uint32_t index,
    scoped_pointer_visit *stack,
    int depth
)
{
    uint32_t start = ts_node_start_byte(declaration);
    for (int i = 0; i < depth; i++) {
        if (stack[i].declaration == start && stack[i].parameter == index) {
            return true; /* Recursion keeps whatever the other uses decide. */
        }
    }
    if (depth == SCOPED_POINTER_DEPTH) {
        return false;
    }
    stack[depth] = (scoped_pointer_visit){ start, index };
    TSNode parameter = ts_node_named_child(field(declaration, "parameters"), index);
    TSNode name = field(parameter, "name");
    return scoped_uses(source, field(declaration, "body"), name, stack, depth + 1);
}
TSNode script_scoped_pointer_target(const char *source, TSNode declaration, uint32_t index)
{
    scoped_pointer_visit stack[SCOPED_POINTER_DEPTH];
    TSNode parameter = ts_node_named_child(field(declaration, "parameters"), index);
    TSNode pointer = pointer_annotation(field(parameter, "type"));
    return !ts_node_is_null(pointer) &&
                   scoped_pointer(source, declaration, index, stack, 0)
             ? field(pointer, "target")
             : (TSNode){ 0 };
}
static void parameters(headers *h, TSNode node, bool results)
{
    int function = h->context.function;
    if (results && !ts_node_is_null(node) && !is(node, "parameter_list")) {
        script_signature_reading read = annotation(h, node, true);
        int symbol = declare(h, "Result", node_range(node, h->context.document), SCRIPT_RESULT, read.type, true);
        h->symbols[symbol].declaration = (script_range){ .start = -1, .end = -1 };
        h->changes[symbol] |= SCRIPT_CALLABLE_RANGE;
        set_unit(h, symbol, read.unit);
        apush(h->functions[function].results, symbol);
        h->function_changes[function] = true;
        return;
    }
    for (uint32_t i = 0; i < named_count(node); i++) {
        TSNode parameter = ts_node_named_child(node, i);
        if (ts_node_is_extra(parameter) ||
            (h->context.instantiating &&
             !ts_node_is_null(field(parameter, "comptime")))) {
            continue;
        }
        script_signature_reading read = annotation(h, field(parameter, "type"), true);
        script_type type = read.type;
        if (!results &&
            !ts_node_is_null(
                script_scoped_pointer_target(h->source, ts_node_parent(node), i)
            )) {
            synchronize(h);
            type = script_catalog_pointee_type(&h->catalog, type);
            type.flags |= SCRIPT_TYPE_REFERENCE;
        }
        if (type.flags & SCRIPT_TYPE_UNTYPED) {
            diagnostic(h, parameter, "Callable parameters and results require a type");
            type = (script_type){ .flags = SCRIPT_TYPE_ERROR };
        }
        int symbol = declare_node(
            h,
            field(parameter, "name"),
            results ? SCRIPT_RESULT : SCRIPT_PARAMETER,
            type,
            true
        );
        set_unit(h, symbol, read.unit);
        if (results) {
            apush(h->functions[function].results, symbol);
        } else {
            apush(h->functions[function].parameters, symbol);
        }
        h->function_changes[function] = true;
        TSNode value = field(parameter, "value");
        if (!ts_node_is_null(value)) {
            apush(
                h->initializers,
                ((script_callable_initializer){ symbol, h->context.document, value })
            );
        }
    }
}
static bool returns_value(TSNode node)
{
    if (is(node, "return_statement")) {
        return !ts_node_is_null(field(node, "value"));
    }
    for (uint32_t i = 0; i < named_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!is(child, "function_declaration") &&
            !is(child, "macro_declaration") && returns_value(child)) {
            return true;
        }
    }
    return false;
}
static void register_header(headers *h, TSNode node, int *symbol_out, int *function_out)
{
    bool macro = is(node, "macro_declaration");
    TSNode name = field(node, "name");
    int symbol = declare_node(
        h,
        name,
        macro ? SCRIPT_MACRO : SCRIPT_FUNCTION,
        (script_type){ .storage = PIN_FLOW },
        false
    );
    *symbol_out = symbol;
    script_range range = node_range(name, h->context.document);
    if (h->facts.named.document_count &&
        (h->symbols[symbol].declaration.document != range.document ||
         h->symbols[symbol].declaration.start != range.start ||
         h->symbols[symbol].kind != (macro ? SCRIPT_MACRO : SCRIPT_FUNCTION))) {
        return;
    }
    int function = function_new(h, node, SCRIPT_FUNCTION_AUTHORED, symbol);
    *function_out = function;
    h->symbols[symbol].function = function;
    h->symbols[symbol].capability = h->symbols[symbol].id;
    h->changes[symbol] |= SCRIPT_CALLABLE_FUNCTION | SCRIPT_CALLABLE_CAPABILITY;
    h->context.function = function;
    TSNode body = field(node, "body");
    apush(
        h->scopes,
        ((script_scope){ h->context.scope, function, node_range(body, h->context.document) })
    );
    h->context.scope = acount(h->scopes) - 1;
    h->functions[function].scope = h->context.scope;
    parameters(h, field(node, "parameters"), false);
    parameters(h, field(node, "results"), true);
    h->functions[function].inferred = !macro &&
                                      ts_node_is_null(field(node, "results")) &&
                                      returns_value(body);
    script_type *results = NULL;
    for (int i = 0; i < acount(h->functions[function].results); i++) {
        apush(results, h->symbols[h->functions[function].results[i]].type);
    }
    h->symbols[symbol]
        .type = script_callable_result_type(h->symbols[symbol].id, results, acount(results));
    h->changes[symbol] |= SCRIPT_CALLABLE_TYPE;
    afree(results);
    apush(h->bodies, ((script_callable_body){ function, h->context.document, body }));
    for (uint32_t i = 0; i < named_count(body); i++) {
        TSNode declaration = ts_node_named_child(body, i);
        if (is(declaration, "entry_declaration")) {
            int index = function_new(h, declaration, SCRIPT_FUNCTION_MACRO_ENTRY, -1);
            h->functions[index].scope = h->context.scope;
            h->functions[index].entry = node_name(h, field(declaration, "name"));
        } else if (is(declaration, "exit_declaration")) {
            int outcome = declare_node(
                h,
                field(declaration, "name"),
                SCRIPT_LABEL,
                (script_type){ .storage = PIN_FLOW },
                false
            );
            h->symbols[outcome].outcome = true;
            h->changes[outcome] |= SCRIPT_CALLABLE_OUTCOME;
        }
    }
}
static script_callable_header finish(headers *h, int symbol, int function)
{
    script_callable_symbol_change *symbols = NULL;
    for (int i = 0; i < acount(h->symbols); i++) {
        if (!h->changes[i]) {
            continue;
        }
        script_symbol row = h->symbols[i];
        row.name = own(h->storage, row.name);
        row.unit = own(h->storage, row.unit);
        apush(symbols, ((script_callable_symbol_change){ i, h->changes[i], row }));
    }
    script_callable_function_change *functions = NULL;
    for (int i = 0; i < acount(h->functions); i++) {
        if (!h->function_changes[i]) {
            continue;
        }
        script_function f = h->functions[i];
        f.entry = own(h->storage, f.entry);
        f.parameter_count = acount(f.parameters);
        f.result_count = acount(f.results);
        f.parameters = copy(
            h->storage,
            f.parameters,
            (size_t)f.parameter_count * sizeof *f.parameters
        );
        f.results = copy(
            h->storage,
            f.results,
            (size_t)f.result_count * sizeof *f.results
        );
        apush(functions, ((script_callable_function_change){ i, i >= h->initial_functions, f }));
    }
    script_callable_header out = { .symbol = symbol,
                                   .function = function,
                                   .symbol_count = acount(symbols),
                                   .function_count = acount(functions),
                                   .scope_count = acount(h->scopes) - h->initial_scopes,
                                   .initializer_count = acount(h->initializers),
                                   .body_count = acount(h->bodies),
                                   .diagnostic_count = acount(h->diagnostics),
                                   .use_count = acount(h->uses) - h->initial_uses,
                                   .type_count = acount(h->own_types) - h->initial_own };
    out.symbols = copy(h->storage, symbols, (size_t)out.symbol_count * sizeof *symbols);
    out.functions = copy(h->storage, functions, (size_t)out.function_count * sizeof *functions);
    out.scopes = copy(
        h->storage,
        out.scope_count ? h->scopes + h->initial_scopes : NULL,
        (size_t)out.scope_count * sizeof *h->scopes
    );
    out.initializers = copy(
        h->storage,
        h->initializers,
        (size_t)out.initializer_count * sizeof *h->initializers
    );
    out.bodies = copy(h->storage, h->bodies, (size_t)out.body_count * sizeof *h->bodies);
    out.diagnostics = copy(
        h->storage,
        h->diagnostics,
        (size_t)out.diagnostic_count * sizeof *h->diagnostics
    );
    out.uses = copy(
        h->storage,
        out.use_count ? h->uses + h->initial_uses : NULL,
        (size_t)out.use_count * sizeof *h->uses
    );
    out.types = copy(
        h->storage,
        out.type_count ? h->own_types + h->initial_own : NULL,
        (size_t)out.type_count * sizeof *h->own_types
    );
    for (int i = 0; i < acount(h->functions); i++) {
        afree(h->functions[i].parameters);
        afree(h->functions[i].results);
    }
    afree(symbols);
    afree(functions);
    afree(h->symbols);
    afree(h->scopes);
    afree(h->functions);
    afree(h->types);
    afree(h->own_types);
    afree(h->uses);
    afree(h->changes);
    afree(h->function_changes);
    afree(h->diagnostics);
    afree(h->initializers);
    afree(h->bodies);
    return out;
}
script_callable_header script_callable_register(
    arena *storage,
    const script_callable_facts *facts,
    TSNode node,
    const char *source,
    script_callable_context context
)
{
    headers h = begin(storage, facts, source, context);
    int symbol = -1, function = -1;
    register_header(&h, node, &symbol, &function);
    return finish(&h, symbol, function);
}
