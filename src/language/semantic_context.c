#include "language/lexical_binding.h"
#include "language/semantic_internal.h"
#include "language/signature_reader.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

const script_syntax *script_analyzer_document_syntax(const analyzer *a, int document)
{
    if (!a->document_count) {
        return a->syntax;
    }
    assert(document >= 0 && document < a->document_count);
    return a->documents[document].syntax;
}
static int node_document(const analyzer *a, TSNode node)
{
    if (!a->document_count || ts_node_is_null(node)) {
        return 0;
    }
    for (int i = 0; i < a->document_count; i++) {
        if (a->documents[i].syntax->tree == node.tree) {
            return i;
        }
    }
    assert(!"semantic node belongs to an unknown document");
    return 0;
}
const script_syntax *script_analyzer_syntax(const analyzer *a, TSNode node)
{
    return script_analyzer_document_syntax(a, node_document(a, node));
}
script_range script_analyzer_range(const analyzer *a, TSNode node)
{
    script_range range = ts_node_is_null(node)
                           ? (script_range){ .line = 1, .column = 1 }
                           : script_node_range(node);
    range.document = node_document(a, node);
    return range;
}

TSNode script_analyzer_field(TSNode node, const char *name)
{
    return ts_node_is_null(node)
             ? (TSNode){ 0 }
             : ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
bool script_analyzer_is(TSNode node, const char *name)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), name);
}
bool script_analyzer_text_is(const analyzer *a, TSNode node, const char *text)
{
    if (ts_node_is_null(node)) {
        return false;
    }
    script_range range = script_analyzer_range(a, node);
    return (size_t)(range.end - range.start) == strlen(text) &&
           !memcmp(script_analyzer_syntax(a, node)->text + range.start, text, strlen(text));
}
char *script_analyzer_text_copy(const analyzer *a, TSNode node)
{
    if (ts_node_is_null(node)) {
        return xstrdup("");
    }
    script_range range = script_analyzer_range(a, node);
    size_t size = (size_t)(range.end - range.start);
    char *text = xmalloc(size + 1);
    memcpy(text, script_analyzer_syntax(a, node)->text + range.start, size);
    text[size] = 0;
    return text;
}
void script_analyzer_diagnostic(analyzer *a, TSNode node, const char *format, ...)
{
    script_diagnostic d = { .range = script_analyzer_range(a, node) };
    va_list args;
    va_start(args, format);
    vsnprintf(d.message, sizeof d.message, format, args);
    va_end(args);
    apush(a->out->diagnostics, d);
}
char *script_analyzer_name_copy(analyzer *a, TSNode node)
{
    char *text = script_analyzer_text_copy(a, node);
    if (*text != '"') {
        return text;
    }
    yyjson_doc *document = yyjson_read(text, strlen(text), 0);
    yyjson_val *value = document ? yyjson_doc_get_root(document) : NULL;
    if (!yyjson_is_str(value) ||
        yyjson_get_len(value) != strlen(yyjson_get_str(value))) {
        script_analyzer_diagnostic(a, node, "Invalid string or embedded NUL");
        yyjson_doc_free(document);
        free(text);
        return xstrdup("");
    }
    char *result = xstrdup(yyjson_get_str(value));
    yyjson_doc_free(document);
    free(text);
    return result;
}
pin_value script_analyzer_string_value(analyzer *a, TSNode node)
{
    char *source = script_analyzer_text_copy(a, node);
    yyjson_doc *document = yyjson_read(source, strlen(source), YYJSON_READ_ALLOW_INVALID_UNICODE);
    yyjson_val *decoded = document ? yyjson_doc_get_root(document) : NULL;
    pin_value value = { .type = PIN_STR };
    const char *error = !yyjson_is_str(decoded) ? "Invalid string literal"
                                                : pin_value_text(
                                                      &a->out->constants,
                                                      &value,
                                                      yyjson_get_str(decoded),
                                                      yyjson_get_len(decoded)
                                                  );
    if (error) {
        script_analyzer_diagnostic(a, node, "%s", error);
    }
    yyjson_doc_free(document);
    free(source);
    return value;
}
void script_analyzer_c_string_argument(analyzer *a, TSNode node, int expression, const char *boundary)
{
    pin_value value;
    if (script_analyzer_scalar_constant(a, expression, PIN_STR, &value)) {
        return;
    }
    pin_string text = pin_text(value);
    const char *nul = text.length ? memchr(text.bytes, 0, text.length) : NULL;
    if (nul) {
        script_analyzer_diagnostic(
            a,
            node,
            "C-string boundary '%s' cannot accept NUL at byte %zu",
            boundary,
            (size_t)(nul - text.bytes)
        );
    }
}
/* A character literal reads as a string literal does, with \' for its own
 * quote, as Odin's rune literals take the string escapes. */
bool script_analyzer_character_value(analyzer *a, TSNode node, int *value)
{
    char *text = script_analyzer_text_copy(a, node);
    size_t size = strlen(text);
    char *json = xmalloc(2 * size + 1);
    size_t length = 0;
    json[length++] = '"';
    for (size_t i = 1; i + 1 < size; i++) {
        if (text[i] == '"') {
            json[length++] = '\\';
        } else if (text[i] == '\\' && text[i + 1] == '\'' && i + 2 < size) {
            i++;
        } else if (text[i] == '\\') {
            json[length++] = text[i++];
        }
        json[length++] = text[i];
    }
    json[length++] = '"';
    yyjson_doc *document = yyjson_read(json, length, 0);
    yyjson_val *string = document ? yyjson_doc_get_root(document) : NULL;
    bool valid = yyjson_is_str(string);
    const unsigned char *bytes = valid ? (const unsigned char *)yyjson_get_str(string)
                                       : NULL;
    size_t count = valid ? yyjson_get_len(string) : 0;
    /* yyjson checked the UTF-8; the lead byte gives the scalar's length. */
    size_t expected = !count          ? 0
                    : bytes[0] < 0x80 ? 1
                    : bytes[0] < 0xe0 ? 2
                    : bytes[0] < 0xf0 ? 3
                                      : 4;
    if (!valid) {
        script_analyzer_diagnostic(a, node, "Invalid character literal");
    } else if (!count || count != expected) {
        script_analyzer_diagnostic(a, node, "A character literal holds exactly one character");
        valid = false;
    } else {
        int scalar = expected == 1 ? bytes[0] : bytes[0] & (0x7f >> expected);
        for (size_t i = 1; i < expected; i++) {
            scalar = (scalar << 6) | (bytes[i] & 0x3f);
        }
        *value = scalar;
    }
    yyjson_doc_free(document);
    free(json);
    free(text);
    return valid;
}
int script_analyzer_scope_new(analyzer *a, TSNode node)
{
    apush(a->out->scopes, ((script_scope){ a->scope, a->function, script_analyzer_range(a, node) }));
    return acount(a->out->scopes) - 1;
}
int script_analyzer_lookup_name(const analyzer *a, const char *name)
{
    script_lexical_facts facts = script_lexical_view(a->out, acount(a->out->symbols));
    return script_lexical_name(&facts, a->scope, name);
}

int script_analyzer_lookup(analyzer *a, TSNode node)
{
    char *name = script_analyzer_name_copy(a, node);
    int result = script_analyzer_lookup_name(a, name);
    free(name);
    return result;
}
int script_analyzer_lookup_storage(analyzer *a, TSNode node, script_symbol_kind kind)
{
    char *name = script_analyzer_name_copy(a, node);
    script_lexical_facts facts = script_lexical_view(a->out, acount(a->out->symbols));
    int found = script_lexical_storage(&facts, a->scope, name, kind);
    free(name);
    return found;
}

static int declare(analyzer *a, TSNode node, char *name, script_symbol_kind kind, script_type type, bool writable)
{
    script_lexical_facts facts = script_lexical_view(a->out, acount(a->out->symbols));
    script_range range = script_analyzer_range(a, node);
    script_lexical_declaration declaration = script_lexical_declare(
        &facts,
        (
            script_lexical_context
        ){ a->scope, a->function, a->package_scope, a->document_count, a->specialization },
        range,
        name,
        kind
    );
    for (int i = 0; i < declaration.diagnostic_count; i++) {
        apush(a->out->diagnostics, declaration.diagnostics[i]);
    }
    if (declaration.existing >= 0) {
        free(name);
        return declaration.existing;
    }
    script_symbol symbol = { .id = declaration.id,
                             .name = name,
                             .kind = kind,
                             .type = type,
                             .declaration = range,
                             .scope = a->scope,
                             .initializer = -1,
                             .function = -1,
                             .writable = writable };
    apush(a->out->symbols, symbol);
    return acount(a->out->symbols) - 1;
}

int script_analyzer_symbol_new(analyzer *a, TSNode node, script_symbol_kind kind, script_type type, bool writable)
{
    return declare(a, node, script_analyzer_name_copy(a, node), kind, type, writable);
}
/* Implicit result names have no authored token for navigation or rename. */
int script_analyzer_result_new(analyzer *a, TSNode node, const char *name, script_type type)
{
    int symbol = declare(a, node, xstrdup(name), SCRIPT_RESULT, type, true);
    a->out->symbols[symbol].declaration = (script_range){ .start = -1, .end = -1 };
    return symbol;
}
void script_analyzer_use(analyzer *a, TSNode node, int symbol, script_use_kind kind, uint64_t member)
{
    apush(a->out->uses, ((script_use){ script_analyzer_range(a, node), symbol, kind, member }));
}
bool script_analyzer_present(const analyzer *a, int symbol)
{
    for (int i = acount(a->narrowed) - 1; i >= 0; i--) {
        if (a->narrowed[i].symbol == symbol) {
            return a->narrowed[i].present &&
                   a->narrowed[i].mutations == a->out->symbols[symbol].mutations;
        }
    }
    return false;
}
script_type script_analyzer_bad_type()
{
    return (script_type){ .flags = SCRIPT_TYPE_ERROR };
}
script_type script_analyzer_boolean_type()
{
    return (script_type){ .storage = PIN_BOOL };
}
void script_analyzer_require(analyzer *a, TSNode node, script_type expected, script_type actual)
{
    if (script_catalog_type_assignable(a->catalog, expected, actual)) {
        return;
    }
    if (script_type_numeric(expected) && script_type_numeric(actual)) {
        /* Integer narrowing, sign changes and floating to integer reach here. */
        script_analyzer_diagnostic(
            a,
            node,
            "Incompatible types: expected %s, got %s; convert with %s(value)",
            script_numeric_name(expected),
            script_numeric_name(actual),
            script_numeric_name(expected)
        );
        return;
    }
    script_analyzer_diagnostic(
        a,
        node,
        "Incompatible types: expected %s%s, got %s%s",
        script_numeric_name(expected),
        expected.flags & SCRIPT_TYPE_OPTIONAL ? "?" : "",
        script_numeric_name(actual),
        actual.flags & SCRIPT_TYPE_OPTIONAL ? "?" : ""
    );
}
const script_expression *script_analyzer_result_source(const analyzer *a, int index)
{
    for (int depth = 0; index >= 0 && depth < SCRIPT_MAX_DEPTH; depth++) {
        const script_expression *value = &a->out->expressions[index];
        if (value->kind == SCRIPT_EXPR_UNWRAP) {
            index = value->left;
            continue;
        }
        if (value->result_count) {
            return value;
        }
        if (value->kind == SCRIPT_EXPR_SYMBOL && value->symbol >= 0) {
            index = a->out->symbols[value->symbol].initializer;
            continue;
        }
        return value;
    }
    return NULL;
}
void script_analyzer_require_values(analyzer *a, TSNode node, int target, int value)
{
    script_type expected = script_analyzer_expr_type(a, target);
    script_type actual = script_analyzer_expr_type(a, value);
    script_analyzer_require(a, node, expected, actual);
    if (!(expected.flags & actual.flags & SCRIPT_TYPE_RESULTS)) {
        return;
    }
    const script_expression *left = script_analyzer_result_source(a, target);
    const script_expression *right = script_analyzer_result_source(a, value);
    if (!left || !right || left->result_count != right->result_count) {
        script_analyzer_diagnostic(a, node, "Incompatible result tuple shape");
        return;
    }
    for (int i = 0; i < left->result_count; i++) {
        script_analyzer_require(a, node, left->results[i], right->results[i]);
    }
}
script_named_facts script_analyzer_named_facts(const analyzer *a)
{
    script_named_facts facts = {
        .lexical = script_lexical_view(a->out, acount(a->out->symbols)),
        .catalog = a->catalog,
        .native = a->native,
        .own_types = a->out->types,
        .own_type_count = acount(a->out->types),
        .imports = a->imports,
        .import_count = a->import_count,
        .document_count = a->document_count,
        .package_scope = a->package_scope,
        .uses = a->out->uses,
        .use_count = acount(a->out->uses)
    };
    const imported_environment *environment = script_analyzer_environment(a);
    if (environment) {
        facts.lexical.identity = environment->environment->identity;
        facts.package_scope = environment->scope;
    }
    return facts;
}
static script_type named_resolution(analyzer *a, script_named_resolution result)
{
    if (result.has_diagnostic) {
        apush(a->out->diagnostics, result.diagnostic);
    }
    if (result.has_use) {
        apush(a->out->uses, result.use);
    }
    return result.type;
}
script_type script_analyzer_conversion_type(analyzer *a, TSNode node, const char *name)
{
    script_named_facts facts = script_analyzer_named_facts(a);
    script_named_request request = { a->scope, script_analyzer_range(a, node) };
    script_named_resolution result = script_named_text(&facts, request, name);
    if (result.has_diagnostic || (result.type.flags & SCRIPT_TYPE_ERROR) ||
        result.type.storage == PIN_FLOW) {
        return script_analyzer_bad_type();
    }
    return named_resolution(a, result);
}
script_type script_analyzer_type_text(analyzer *a, TSNode node, const char *source)
{
    script_named_facts facts = script_analyzer_named_facts(a);
    script_named_request request = { a->scope, script_analyzer_range(a, node) };
    return named_resolution(a, script_named_text(&facts, request, source));
}

/* Evaluates the compile-time type applications and generic members a type
 * names, outermost first, so the signature reader can read the rest. */
void script_analyzer_generic_types(analyzer *a, TSNode node, script_signature_generic **out)
{
    if (ts_node_is_null(node)) {
        return;
    }
    if (script_analyzer_is(node, "type_name")) {
        char *name = script_analyzer_name_copy(a, node);
        script_math_kind kind = script_math_named(name);
        free(name);
        if (kind) {
            (void)script_analyzer_math_type(a, node, kind);
        }
    }
    /* The reader constructs `task(T)` itself; T may hold applications. */
    if (script_analyzer_is(node, "type_application") &&
        !script_signature_task(script_analyzer_syntax(a, node)->text, node)) {
        script_signature_generic resolved = { ts_node_start_byte(node),
                                              ts_node_end_byte(node),
                                              script_analyzer_generic_type(a, node) };
        apush(*out, resolved);
        return;
    }
    TSNode name = script_analyzer_field(node, "name");
    if (script_analyzer_is(node, "type") && script_analyzer_is(name, "type_name")) {
        char *text = script_analyzer_name_copy(a, name);
        char *dot = strchr(text, '.');
        if (dot) {
            *dot = '\0';
            int owner = script_analyzer_lookup_name(a, text);
            compile_value member;
            if (owner >= 0 && a->out->symbols[owner].kind == SCRIPT_TYPE &&
                script_analyzer_generic_member(a, a->out->symbols[owner].type, dot + 1, &member) &&
                member.is_type) {
                apush(
                    *out,
                    ((script_signature_generic){ ts_node_start_byte(name),
                                                 ts_node_end_byte(name),
                                                 member.type })
                );
            }
        }
        free(text);
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        script_analyzer_generic_types(a, ts_node_named_child(node, i), out);
    }
}
script_type script_analyzer_read_type(analyzer *a, TSNode node, bool signature, char **unit)
{
    script_signature_generic *generics = NULL;
    script_analyzer_generic_types(a, node, &generics);
    script_signature_facts facts = { .named = script_analyzer_named_facts(a),
                                     .functions = a->out->functions,
                                     .function_count = acount(a->out->functions),
                                     .pointer_size = sizeof(void *),
                                     .generics = generics,
                                     .generic_count = acount(generics) };
    script_signature_reading reading = script_signature_read(
        &a->out->constants,
        &facts,
        node,
        script_analyzer_syntax(a, node)->text,
        a->scope,
        script_analyzer_range(a, node).document,
        signature
    );
    for (int i = 0; i < reading.diagnostic_count; i++) {
        apush(a->out->diagnostics, reading.diagnostics[i]);
    }
    for (int i = 0; i < reading.use_count; i++) {
        apush(a->out->uses, reading.uses[i]);
    }
    for (int i = 0; i < reading.type_count; i++) {
        script_analyzer_publish_type(a, reading.types[i]);
    }
    if (unit) {
        *unit = reading.unit ? xstrdup(reading.unit) : NULL;
    }
    afree(generics);
    return reading.type;
}
script_type script_analyzer_collection_type(analyzer *a, pin_container container, script_type key, script_type value)
{
    script_catalog_type definition = script_constructor_collection(
        &a->out->constants,
        container,
        key,
        value,
        sizeof(void *)
    );
    if (!script_catalog_type_by_id(a->catalog, definition.id)) {
        script_analyzer_publish_type(a, definition);
    }
    return definition.type;
}

void script_analyzer_invalidate_pointers(analyzer *a)
{
    for (int i = 0; i < acount(a->out->symbols); i++) {
        if (a->out->symbols[i].addressed) {
            a->out->symbols[i].mutations++;
        }
    }
}
script_type script_analyzer_pointer_type(analyzer *a, script_type pointee)
{
    script_catalog_type definition = script_constructor_pointer(
        &a->out->constants,
        pointee,
        sizeof(void *)
    );
    if (!script_catalog_type_by_id(a->catalog, definition.id)) {
        script_analyzer_publish_type(a, definition);
    }
    return definition.type;
}
script_type script_analyzer_type_node(analyzer *a, TSNode node)
{
    return script_analyzer_read_type(a, node, false, NULL);
}
int script_analyzer_expression(analyzer *, TSNode, script_type);
int script_analyzer_statement(analyzer *, TSNode);
int script_analyzer_expr_add(analyzer *a, script_expression expression)
{
    expression.argument_count = acount(expression.arguments);
    apush(a->out->expressions, expression);
    return acount(a->out->expressions) - 1;
}
script_type script_analyzer_expr_type(const analyzer *a, int expression)
{
    return expression >= 0 ? a->out->expressions[expression].type
                           : script_analyzer_bad_type();
}
unsigned script_analyzer_expr_effects(const analyzer *a, int expression)
{
    return expression >= 0 ? a->out->expressions[expression].effects : 0;
}
int script_analyzer_initializer_find(const analyzer *a, int symbol)
{
    for (int i = 0; i < acount(a->initializers); i++) {
        if (a->initializers[i].symbol == symbol) {
            return i;
        }
    }
    return -1;
}
/* Tasks, handles and arrays start empty without a written default. */
static bool starts_empty(script_type type)
{
    if (type.container == PIN_CONTAINER_VIEW) {
        return (type.flags & SCRIPT_TYPE_OPTIONAL) != 0;
    }
    return (type.flags & SCRIPT_TYPE_TASK) || type.storage == PIN_HANDLE ||
           (type.storage == PIN_POINTER && (type.flags & SCRIPT_TYPE_OPTIONAL)) ||
           type.container != PIN_CONTAINER_SINGLE;
}
bool script_analyzer_compile_time_value(analyzer *a, int index, int *steps, bool scalar)
{
    while (acount(a->constant_proofs) < acount(a->out->expressions)) {
        apush(a->constant_proofs, ((constant_proof){ 0 }));
    }
    if (!++a->constant_generation) {
        for (int i = 0; i < acount(a->constant_proofs); i++) {
            a->constant_proofs[i].generation = 0;
        }
        a->constant_generation = 1;
    }
    return script_constant_prove(a->out, a->constant_proofs, a->constant_generation, index, steps, scalar);
}
bool script_analysis_constant(const script_analysis *analysis, int expression)
{
    if (!analysis || expression < 0 || expression >= analysis->expression_count) {
        return false;
    }
    /* The shared proof reads the analysis; only this temporary analyzer owns
     * and changes its proof cache. */
    analyzer a = { .out = (script_analysis *)analysis };
    int steps = 65536;
    bool constant = script_analyzer_compile_time_value(&a, expression, &steps, false);
    afree(a.constant_proofs);
    return constant;
}
void script_analyzer_resolve_initializer(analyzer *a, int symbol)
{
    script_symbol *record = &a->out->symbols[symbol];
    int index = script_analyzer_initializer_find(a, symbol);
    if (index < 0 || record->initializer >= 0) {
        return;
    }
    if (record->resolving) {
        script_analyzer_diagnostic(
            a,
            a->initializers[index].value,
            "Cyclic value dependency involving '%s'",
            record->name
        );
        record->type = script_analyzer_bad_type();
        return;
    }
    record->resolving = true;
    int old_scope = a->scope;
    a->scope = record->scope;
    script_type expected = record->type;
    TSNode node = a->initializers[index].value;
    int value;
    if (ts_node_is_null(node) && starts_empty(expected) &&
        !(expected.flags & SCRIPT_TYPE_UNTYPED)) {
        value = script_analyzer_expr_add(
            a,
            (script_expression){
                .kind = expected.container == PIN_CONTAINER_VIEW ? SCRIPT_EXPR_NIL
                      : expected.container == PIN_CONTAINER_MAP ? SCRIPT_EXPR_MAP
                      : expected.container != PIN_CONTAINER_SINGLE ? SCRIPT_EXPR_ARRAY
                                                                   : SCRIPT_EXPR_CONSTANT,
                .type = expected,
                .constant = { .type = expected.storage, .container = expected.container },
                .range = record->declaration,
                .symbol = -1,
                .left = -1,
                .right = -1,
                .entry = -1 }
        );
    } else if (ts_node_is_null(node)) {
        bool view = expected.container == PIN_CONTAINER_VIEW &&
                    !(expected.flags & SCRIPT_TYPE_OPTIONAL);
        script_analyzer_diagnostic(
            a,
            node,
            view ? "A nonoptional view requires an initializer"
                 : "'%s' requires a default value",
            record->name
        );
        value = script_analyzer_expression(a, node, expected);
    } else {
        value = script_analyzer_expression(a, node, expected);
    }
    a->scope = old_scope;
    record = &a->out->symbols[symbol];
    record->initializer = value;
    record->resolving = false;
    record->capability = a->out->expressions[value].capability;
    if (a->out->expressions[value].kind == SCRIPT_EXPR_CALL &&
        a->out->expressions[value].right >= 0) {
        record->function = a->out->expressions[value].right;
    }
    if (expected.flags & SCRIPT_TYPE_UNTYPED) {
        record->type = script_analyzer_expr_type(a, value);
        record->type.flags &= ~SCRIPT_TYPE_UNTYPED;
        if (record->kind != SCRIPT_CONSTANT) {
            record->type.flags &= ~SCRIPT_TYPE_LITERAL;
        }
    } else {
        script_type actual = script_analyzer_expr_type(a, value);
        script_analyzer_require(a, a->initializers[index].value, expected, actual);
        if (record->kind == SCRIPT_CONSTANT && script_type_numeric(expected) &&
            script_type_numeric(actual) && actual.storage != expected.storage) {
            /* A typed constant holds its declared storage. */
            value = script_analyzer_expr_add(
                a,
                (script_expression){ .kind = SCRIPT_EXPR_CAST,
                                     .type = expected,
                                     .range = a->out->expressions[value].range,
                                     .left = value,
                                     .right = -1,
                                     .symbol = -1,
                                     .entry = -1 }
            );
            record = &a->out->symbols[symbol];
            record->initializer = value;
        }
    }
    if (record->kind == SCRIPT_SHARED_STATE &&
        script_type_contains_tasks(a->catalog, record->type)) {
        /* A task belongs to the entity that started it. */
        script_analyzer_diagnostic(a, node, "System-owned retained bindings cannot contain tasks; keep task handles in coroutine locals");
    }
    if (record->kind == SCRIPT_ACTION && a->out->expressions[value].result_count) {
        record->type = (script_type){ .storage = PIN_STRUCT,
                                      .nominal = record->capability,
                                      .flags = SCRIPT_TYPE_RESULTS };
    }
    if (record->kind == SCRIPT_CONSTANT &&
        (!script_analyzer_compile_time_value(a, value, NULL, false) ||
         record->type.storage == PIN_FLOW ||
         (record->type.flags &
          (SCRIPT_TYPE_TASK | SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_RESULTS)))) {
        script_analyzer_diagnostic(a, node, "Constant initializer requires compile-time values and operators");
    }
    if ((record->kind == SCRIPT_STATE || record->kind == SCRIPT_SHARED_STATE) &&
        script_analyzer_expr_effects(a, value)) {
        script_analyzer_diagnostic(a, a->initializers[index].value, "State defaults must be constant and deterministic");
    }
}
static bool contains_task(const script_catalog *catalog, script_type type, uint64_t *path, int depth)
{
    if (type.storage == PIN_TASK || (type.flags & SCRIPT_TYPE_TASK)) {
        return true;
    }
    if (depth == SCRIPT_MAX_DEPTH) {
        return true;
    }
    path[depth] = 0;
    if (type.container >= PIN_CONTAINER_ARRAY) {
        if (type.container == PIN_CONTAINER_MAP &&
            contains_task(catalog, script_catalog_key_type(catalog, type), path, depth + 1)) {
            return true;
        }
        return contains_task(catalog, script_catalog_element_type(catalog, type), path, depth + 1);
    }
    const script_catalog_type *record = script_catalog_type_by_id(catalog, type.nominal);
    if (!record) {
        return false;
    }
    for (int i = 0; i < depth; i++) {
        if (path[i] == record->id) {
            return false;
        }
    }
    path[depth] = record->id;
    for (int i = 0; i < record->field_count; i++) {
        if (contains_task(catalog, record->fields[i].type, path, depth + 1)) {
            return true;
        }
    }
    for (int i = 0; i < record->variant_count; i++) {
        const script_catalog_variant *variant = &record->variants[i];
        if (!variant->tagged &&
            contains_task(catalog, variant->type, path, depth + 1)) {
            return true;
        }
        for (int j = 0; j < variant->field_count; j++) {
            if (contains_task(catalog, variant->fields[j].type, path, depth + 1)) {
                return true;
            }
        }
    }
    return false;
}
bool script_type_contains_tasks(const script_catalog *catalog, script_type type)
{
    uint64_t path[SCRIPT_MAX_DEPTH] = { 0 };
    return contains_task(catalog, type, path, 0);
}
script_operator script_analyzer_operator_node(analyzer *a, TSNode node)
{
    const struct
    {
        const char *text;
        script_operator op;
    } entries[] = {
        { "+", SCRIPT_OP_ADD },         { "-", SCRIPT_OP_SUB },
        { "*", SCRIPT_OP_MUL },         { "/", SCRIPT_OP_DIV },
        { "%", SCRIPT_OP_MOD },         { "!", SCRIPT_OP_NOT },
        { "&&", SCRIPT_OP_AND },        { "||", SCRIPT_OP_OR },
        { "and", SCRIPT_OP_AND },       { "or", SCRIPT_OP_OR },
        { "not", SCRIPT_OP_NOT },       { "==", SCRIPT_OP_EQ },
        { "!=", SCRIPT_OP_NE },         { "<", SCRIPT_OP_LT },
        { "<=", SCRIPT_OP_LE },         { ">", SCRIPT_OP_GT },
        { ">=", SCRIPT_OP_GE },         { "&", SCRIPT_OP_BIT_AND },
        { "|", SCRIPT_OP_BIT_OR },      { "^", SCRIPT_OP_BIT_XOR },
        { "<<", SCRIPT_OP_SHIFT_LEFT }, { ">>", SCRIPT_OP_SHIFT_RIGHT },
        { "~", SCRIPT_OP_BIT_NOT },     { "&=", SCRIPT_OP_BIT_AND },
        { "|=", SCRIPT_OP_BIT_OR },     { "^=", SCRIPT_OP_BIT_XOR },
        { "+=", SCRIPT_OP_ADD },        { "-=", SCRIPT_OP_SUB },
        { "*=", SCRIPT_OP_MUL },        { "/=", SCRIPT_OP_DIV },
        { "%=", SCRIPT_OP_MOD },        { "??", SCRIPT_OP_COALESCE }
    };
    for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) {
        if (script_analyzer_text_is(a, node, entries[i].text)) {
            return entries[i].op;
        }
    }
    return SCRIPT_OP_NONE;
}
