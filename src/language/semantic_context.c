#include "language/lexical_binding.h"
#include "language/semantic_internal.h"
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
        (script_lexical_context){ a->scope, a->function, a->package_scope, a->document_count },
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
    if (!script_catalog_type_assignable(a->catalog, expected, actual)) {
        script_analyzer_diagnostic(
            a,
            node,
            "Incompatible types: expected %s%s, got %s%s",
            script_scalar_name(expected.storage),
            expected.flags & SCRIPT_TYPE_OPTIONAL ? "?" : "",
            script_scalar_name(actual.storage),
            actual.flags & SCRIPT_TYPE_OPTIONAL ? "?" : ""
        );
    }
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
    return (script_named_facts){
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
static script_type optional_storage(const analyzer *a, script_type type)
{
    const script_catalog_type *definition = script_catalog_type_by_id(a->catalog, type.nominal);
    if ((type.flags & SCRIPT_TYPE_OPTIONAL) && (!definition || !definition->component)) {
        type.flags |= SCRIPT_TYPE_BOXED_OPTIONAL;
    }
    return type;
}
script_type script_analyzer_type_text(analyzer *a, TSNode node, const char *source)
{
    script_named_facts facts = script_analyzer_named_facts(a);
    script_named_request request = { a->scope, script_analyzer_range(a, node) };
    return named_resolution(a, script_named_text(&facts, request, source));
}

script_type script_analyzer_type_node(analyzer *a, TSNode node)
{
    if (ts_node_is_null(node)) {
        return (script_type){ .flags = SCRIPT_TYPE_UNTYPED };
    }
    if (script_analyzer_is(node, "type")) {
        for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
            TSNode child = ts_node_named_child(node, i);
            if (script_analyzer_is(child, "union_type")) {
                return script_analyzer_type_node(a, child);
            }
        }
        TSNode name_node = script_analyzer_field(node, "name"),
               arguments = script_analyzer_field(node, "arguments");
        char *name = script_analyzer_name_copy(a, name_node);
        bool collection = !strcmp(name, "array") || !strcmp(name, "set") ||
                          !strcmp(name, "map");
        bool nominal = !strcmp(name, "struct") || !strcmp(name, "enum") ||
                       !strcmp(name, "handle");
        script_type type;
        if (!strcmp(name, "task") && !ts_node_is_null(arguments)) {
            if (script_analyzer_named_count(arguments) != 1) {
                script_analyzer_diagnostic(a, node, "task requires one result type");
                type = script_analyzer_bad_type();
            } else {
                script_type result = script_analyzer_type_node(a, ts_node_named_child(arguments, 0));
                type = script_analyzer_task_type(a, result, 0);
            }
        } else if (collection || nominal) {
            TSNode argument = ts_node_named_child(arguments, 0);
            if (ts_node_is_null(argument)) {
                script_analyzer_diagnostic(a, node, "Type '%s' requires a type argument", name);
                type = script_analyzer_bad_type();
            } else {
                type = script_analyzer_type_node(a, argument);
            }
            if (collection) {
                type.container = !strcmp(name, "set") ? PIN_CONTAINER_SET
                               : !strcmp(name, "map") ? PIN_CONTAINER_MAP
                                                      : PIN_CONTAINER_ARRAY;
            } else if (!strcmp(name, "enum")) {
                type.storage = PIN_ENUM;
            } else if (!strcmp(name, "handle")) {
                type.storage = PIN_HANDLE;
            }
        } else {
            char **tokens = NULL;
            for (uint32_t i = 0; i < script_analyzer_child_count(node); i++) {
                apush(tokens, script_analyzer_text_copy(a, ts_node_child(node, i)));
            }
            script_named_facts facts = script_analyzer_named_facts(a);
            script_named_request request = { a->scope,
                                             script_analyzer_range(a, name_node) };
            script_named_resolution result = script_named_parsed(
                &facts,
                request,
                name,
                (const char *const *)tokens,
                acount(tokens)
            );
            for (int i = 0; i < acount(tokens); i++) {
                free(tokens[i]);
            }
            afree(tokens);
            free(name);
            return named_resolution(a, result);
        }
        for (uint32_t i = 0; i < script_analyzer_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            if (script_analyzer_text_is(a, child, "ref")) {
                type.flags |= SCRIPT_TYPE_REFERENCE;
            } else if (script_analyzer_text_is(a, child, "?")) {
                type.flags |= SCRIPT_TYPE_OPTIONAL;
            } else if (script_analyzer_text_is(a, child, "[") || script_analyzer_text_is(a, child, "[]")) {
                type.container = PIN_CONTAINER_ARRAY;
            }
        }
        free(name);
        return optional_storage(a, type);
    }
    if (script_analyzer_is(node, "union_type")) {
        script_type *members = NULL;
        for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
            TSNode child = ts_node_named_child(node, i);
            if (!ts_node_is_extra(child)) {
                apush(members, script_analyzer_type_node(a, child));
            }
        }
        script_type type = script_analyzer_union_type(a, node, members, acount(members));
        afree(members);
        return type;
    }
    char *text = script_analyzer_text_copy(a, node);
    script_type type = script_analyzer_type_text(a, node, text);
    free(text);
    return type;
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
    return (type.flags & SCRIPT_TYPE_TASK) || type.storage == PIN_HANDLE ||
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
    if (ts_node_is_null(node) && starts_empty(expected)) {
        value = script_analyzer_expr_add(
            a,
            (script_expression){ .kind = expected.container != PIN_CONTAINER_SINGLE
                                           ? SCRIPT_EXPR_ARRAY
                                           : SCRIPT_EXPR_CONSTANT,
                                 .type = expected,
                                 .constant = { .type = expected.storage,
                                               .container = expected.container },
                                 .range = record->declaration,
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .entry = -1 }
        );
    } else if (ts_node_is_null(node)) {
        script_analyzer_diagnostic(
            a,
            node,
            "'%s' requires a default value",
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
        script_analyzer_require(
            a,
            a->initializers[index].value,
            expected,
            script_analyzer_expr_type(a, value)
        );
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
        { "%=", SCRIPT_OP_MOD }
    };
    for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) {
        if (script_analyzer_text_is(a, node, entries[i].text)) {
            return entries[i].op;
        }
    }
    return SCRIPT_OP_NONE;
}
