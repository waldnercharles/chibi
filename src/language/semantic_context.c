#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

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
    script_range range = script_analyzer_range(node);
    return (size_t)(range.end - range.start) == strlen(text) &&
           !memcmp(a->syntax->text + range.start, text, strlen(text));
}
char *script_analyzer_text_copy(const analyzer *a, TSNode node)
{
    if (ts_node_is_null(node)) {
        return xstrdup("");
    }
    script_range range = script_analyzer_range(node);
    size_t size = (size_t)(range.end - range.start);
    char *text = xmalloc(size + 1);
    memcpy(text, a->syntax->text + range.start, size);
    text[size] = 0;
    return text;
}
void script_analyzer_diagnostic(analyzer *a, TSNode node, const char *format, ...)
{
    script_diagnostic d = { .range = script_analyzer_range(node) };
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
    apush(a->out->scopes, ((script_scope){ a->scope, a->function, script_analyzer_range(node) }));
    return acount(a->out->scopes) - 1;
}
int script_analyzer_lookup_name(const analyzer *a, const char *name)
{
    for (int scope = a->scope; scope >= 0; scope = a->out->scopes[scope].parent) {
        for (int i = acount(a->out->symbols) - 1; i >= 0; i--) {
            const script_symbol *symbol = &a->out->symbols[i];
            if (symbol->scope == scope && symbol->kind != SCRIPT_FUNCTION &&
                symbol->kind != SCRIPT_MACRO && !strcmp(symbol->name, name)) {
                return i;
            }
        }
    }
    return -1;
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
    int found = -1;
    for (int scope = a->scope; scope >= 0 && found < 0;
         scope = a->out->scopes[scope].parent) {
        for (int i = acount(a->out->symbols) - 1; i >= 0; i--) {
            const script_symbol *symbol = &a->out->symbols[i];
            if (symbol->scope == scope && !strcmp(symbol->name, name) &&
                (symbol->kind == kind ||
                 (kind == SCRIPT_STATE && symbol->kind == SCRIPT_SHARED_STATE))) {
                found = i;
                break;
            }
        }
    }
    free(name);
    return found;
}
static int declare(analyzer *a, TSNode node, char *name, script_symbol_kind kind, script_type type, bool writable)
{
    int existing = script_analyzer_lookup_name(a, name);
    if (kind == SCRIPT_FUNCTION || kind == SCRIPT_MACRO) {
        existing = -1;
        for (int i = acount(a->out->symbols) - 1; i >= 0; i--) {
            const script_symbol *candidate = &a->out->symbols[i];
            if (candidate->scope == a->scope &&
                (candidate->kind == SCRIPT_FUNCTION || candidate->kind == SCRIPT_MACRO) &&
                !strcmp(candidate->name, name)) {
                existing = i;
                break;
            }
        }
    }
    bool result_parameter = existing >= 0 &&
                            (kind == SCRIPT_RESULT || kind == SCRIPT_LOCAL) &&
                            a->out->symbols[existing].kind == SCRIPT_PARAMETER;
    if (existing >= 0 && a->out->symbols[existing].scope == a->scope &&
        !result_parameter) {
        script_analyzer_diagnostic(a, node, "Duplicate declaration '%s'", name);
        free(name);
        return existing;
    }
    script_symbol symbol = { .name = name,
                             .kind = kind,
                             .type = type,
                             .declaration = script_analyzer_range(node),
                             .scope = a->scope,
                             .initializer = -1,
                             .function = -1,
                             .writable = writable };
    const char *program = a->out->identity ? a->out->identity : "";
    size_t size = strlen(program) + strlen(name) + 32;
    char *identity = xmalloc(size);
    const char *domain = kind == SCRIPT_FUNCTION || kind == SCRIPT_MACRO
                           ? ":callable"
                       : result_parameter ? kind == SCRIPT_LOCAL ? ":local" : ":result"
                                          : "";
    snprintf(identity, size, "%s:%d:%s%s", program, a->scope, name, domain);
    symbol.id = script_catalog_id("symbol", identity);
    free(identity);
    apush(a->out->symbols, symbol);
    return acount(a->out->symbols) - 1;
}
int script_analyzer_symbol_new(analyzer *a, TSNode node, script_symbol_kind kind, script_type type, bool writable)
{
    return declare(a, node, script_analyzer_name_copy(a, node), kind, type, writable);
}
/* A result named by the language rather than the source, declared at `node`. */
int script_analyzer_result_new(analyzer *a, TSNode node, const char *name, script_type type)
{
    return declare(a, node, xstrdup(name), SCRIPT_RESULT, type, true);
}
void script_analyzer_use(analyzer *a, TSNode node, int symbol, script_use_kind kind, uint64_t member)
{
    apush(a->out->uses, ((script_use){ script_analyzer_range(node), symbol, kind, member }));
}
bool script_analyzer_present(const analyzer *a, int symbol)
{
    for (int i = acount(a->narrowed) - 1; i >= 0; i--) {
        if (a->narrowed[i].symbol == symbol) {
            return a->narrowed[i].present;
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
static void type_use(analyzer *a, TSNode node, int symbol, uint64_t nominal)
{
    if (symbol < 0 && !nominal) {
        return;
    }
    script_range range = script_analyzer_range(node);
    for (int i = 0; i < acount(a->out->uses); i++) {
        const script_use *use = &a->out->uses[i];
        if (use->kind == SCRIPT_TYPE_USE && use->symbol == symbol &&
            use->member == nominal && use->range.start == range.start &&
            use->range.end == range.end) {
            return;
        }
    }
    script_analyzer_use(a, node, symbol, SCRIPT_TYPE_USE, nominal);
}
script_type script_analyzer_type_text(analyzer *a, TSNode node, const char *source)
{
    size_t length = strlen(source);
    char *name = xmalloc(length + 1);
    int count = 0;
    for (size_t i = 0; i < length; i++) {
        if (!isspace((unsigned char)source[i])) {
            name[count++] = source[i];
        }
    }
    name[count] = 0;
    unsigned flags = 0;
    if (!strncmp(name, "ref", 3) && count > 3) {
        memmove(name, name + 3, (size_t)count - 2);
        count -= 3;
        flags |= SCRIPT_TYPE_REFERENCE;
    }
    if (count && name[count - 1] == '?') {
        name[--count] = 0;
        flags |= SCRIPT_TYPE_OPTIONAL;
    }
    pin_container container = PIN_CONTAINER_SINGLE;
    if (count >= 2 && !strcmp(name + count - 2, "[]")) {
        name[count -= 2] = 0;
        container = PIN_CONTAINER_ARRAY;
    }
    const struct
    {
        const char *name;
        pin_container container;
    } containers[] = { { "set<", PIN_CONTAINER_SET },
                       { "map<", PIN_CONTAINER_MAP },
                       { "array<", PIN_CONTAINER_ARRAY } };
    for (size_t i = 0; i < sizeof containers / sizeof *containers; i++) {
        size_t prefix = strlen(containers[i].name);
        if (!strncmp(name, containers[i].name, prefix) && count > (int)prefix &&
            name[count - 1] == '>') {
            container = containers[i].container;
            name[count - 1] = 0;
            memmove(name, name + prefix, (size_t)count - prefix);
            count -= (int)prefix + 1;
            break;
        }
    }
    if ((!strncmp(name, "struct<", 7) || !strncmp(name, "handle<", 7)) &&
        count > 7 && name[count - 1] == '>') {
        name[count - 1] = 0;
        memmove(name, name + 7, (size_t)count - 7);
    } else if (!strncmp(name, "enum<", 5) && count > 5 && name[count - 1] == '>') {
        name[count - 1] = 0;
        memmove(name, name + 5, (size_t)count - 5);
    }
    if (*name == '"') {
        yyjson_doc *d = yyjson_read(name, strlen(name), 0);
        if (d && yyjson_is_str(yyjson_doc_get_root(d))) {
            char *decoded = xstrdup(yyjson_get_str(yyjson_doc_get_root(d)));
            free(name);
            name = decoded;
        }
        yyjson_doc_free(d);
    }
    script_type type = script_builtin_type(name);
    if (type.flags & SCRIPT_TYPE_ERROR) {
        int symbol = script_analyzer_lookup_name(a, name);
        if (symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_TYPE_PARAMETER) {
            type = a->out->symbols[symbol].type;
        } else {
            int index = script_catalog_find_type(a->catalog, name);
            if (index < 0) {
                script_analyzer_diagnostic(a, node, "%s type '%s'", index == -2 ? "Ambiguous" : "Unknown", name);
            } else {
                type = a->catalog->types[index].type;
                type.nominal = a->catalog->types[index].id;
            }
        }
    }
    if (!strcmp(name, "entity") && a->catalog->entity_type) {
        type.nominal = a->catalog->entity_type;
    }
    type.container = container;
    type.flags |= flags;
    int symbol = script_analyzer_lookup_name(a, name);
    if (symbol < 0 || a->out->symbols[symbol].kind != SCRIPT_TYPE_PARAMETER) {
        symbol = -1;
    }
    if (!(type.flags & SCRIPT_TYPE_ERROR)) {
        type_use(a, node, symbol, symbol < 0 ? type.nominal : 0);
    }
    free(name);
    return type;
}
script_type script_analyzer_type_node(analyzer *a, TSNode node)
{
    if (ts_node_is_null(node)) {
        return (script_type){ .flags = SCRIPT_TYPE_UNTYPED };
    }
    if (script_analyzer_is(node, "type")) {
        TSNode name_node = script_analyzer_field(node, "name"),
               arguments = script_analyzer_field(node, "arguments");
        char *name = script_analyzer_name_copy(a, name_node);
        bool collection = !strcmp(name, "array") || !strcmp(name, "set") ||
                          !strcmp(name, "map");
        bool nominal = !strcmp(name, "struct") || !strcmp(name, "enum") ||
                       !strcmp(name, "handle");
        script_type type;
        if (collection || nominal) {
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
            type = script_builtin_type(name);
            int symbol = script_analyzer_lookup_name(a, name);
            if (symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_TYPE_PARAMETER) {
                type = a->out->symbols[symbol].type;
            } else if (type.flags & SCRIPT_TYPE_ERROR) {
                int index = script_catalog_find_type(a->catalog, name);
                if (index >= 0) {
                    type = a->catalog->types[index].type;
                    type.nominal = a->catalog->types[index].id;
                } else {
                    script_analyzer_diagnostic(
                        a,
                        name_node,
                        "%s type '%s'",
                        index == -2 ? "Ambiguous" : "Unknown",
                        name
                    );
                }
            }
            if (!strcmp(name, "entity") && a->catalog->entity_type) {
                type.nominal = a->catalog->entity_type;
            }
            if (!(type.flags & SCRIPT_TYPE_ERROR)) {
                bool generic = symbol >= 0 && a->out->symbols[symbol].kind ==
                                                  SCRIPT_TYPE_PARAMETER;
                type_use(a, name_node, generic ? symbol : -1, generic ? 0 : type.nominal);
            }
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
    if ((record->kind == SCRIPT_STATE || record->kind == SCRIPT_SHARED_STATE) &&
        script_analyzer_expr_effects(a, value)) {
        script_analyzer_diagnostic(a, a->initializers[index].value, "State defaults must be constant and deterministic");
    }
    if (record->kind == SCRIPT_LAZY &&
        script_analyzer_expr_effects(a, value) &
            (SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_STRUCTURAL |
             SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND)) {
        script_analyzer_diagnostic(a, a->initializers[index].value, "Lazy values cannot perform execution effects");
    }
}
script_operator script_analyzer_operator_node(analyzer *a, TSNode node)
{
    const struct
    {
        const char *text;
        script_operator op;
    } entries[] = {
        { "+", SCRIPT_OP_ADD },          { "-", SCRIPT_OP_SUB },
        { "*", SCRIPT_OP_MUL },          { "/", SCRIPT_OP_DIV },
        { "%", SCRIPT_OP_MOD },          { "!", SCRIPT_OP_NOT },
        { "&&", SCRIPT_OP_AND },         { "||", SCRIPT_OP_OR },
        { "==", SCRIPT_OP_EQ },          { "!=", SCRIPT_OP_NE },
        { "<", SCRIPT_OP_LT },           { "<=", SCRIPT_OP_LE },
        { ">", SCRIPT_OP_GT },           { ">=", SCRIPT_OP_GE },
        { "&", SCRIPT_OP_BIT_AND },      { "|", SCRIPT_OP_BIT_OR },
        { "^", SCRIPT_OP_BIT_XOR },      { "<<", SCRIPT_OP_SHIFT_LEFT },
        { ">>", SCRIPT_OP_SHIFT_RIGHT }, { "~", SCRIPT_OP_BIT_NOT },
        { "&=", SCRIPT_OP_BIT_AND },     { "|=", SCRIPT_OP_BIT_OR },
        { "^=", SCRIPT_OP_BIT_XOR },     { "+=", SCRIPT_OP_ADD },
        { "-=", SCRIPT_OP_SUB },         { "*=", SCRIPT_OP_MUL },
        { "/=", SCRIPT_OP_DIV },         { "%=", SCRIPT_OP_MOD }
    };
    for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) {
        if (script_analyzer_text_is(a, node, entries[i].text)) {
            return entries[i].op;
        }
    }
    return SCRIPT_OP_NONE;
}
