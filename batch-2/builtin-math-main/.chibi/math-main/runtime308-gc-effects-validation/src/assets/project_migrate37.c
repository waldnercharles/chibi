#include "project_migrate37.h"

#include "project_migrate26.h"
#include "support/array.h"
#include "support/text_edit.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    TSNode name;
    bool optional;
} component_binding;

typedef struct
{
    const char *text;
    text_edit *edits;
    char **owned;
    TSNode root;
    TSNode refused;
    const char *reason;
} query_rewrite;

static bool is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), type);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_is_null(node)
             ? node
             : ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static void edit(query_rewrite *s, TSNode node, const char *text)
{
    apush(s->edits, ((text_edit){ (int)ts_node_start_byte(node), (int)ts_node_end_byte(node), text }));
}
static void insert(query_rewrite *s, uint32_t at, const char *text)
{
    apush(s->edits, ((text_edit){ (int)at, (int)at, text }));
}
static void refuse(query_rewrite *s, TSNode node, const char *reason)
{
    if (!s->reason) {
        s->refused = node;
        s->reason = reason;
    }
}
static TSNode token(TSNode node, const char *type)
{
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        if (is(child, type)) {
            return child;
        }
    }
    return (TSNode){ 0 };
}
static bool named(const char *text, TSNode node, const char *want)
{
    for (uint32_t i = ts_node_start_byte(node); i < ts_node_end_byte(node); i++) {
        char c = text[i];
        if (c == ' ' || c == '_' || c == '-' || c == '"') {
            continue;
        }
        if (!*want || tolower((unsigned char)c) != *want++) {
            return false;
        }
    }
    return !*want;
}
/* Keep trivia, especially comments within a removed step payload. */
static void remove_tokens(query_rewrite *s, TSNode node)
{
    if (ts_node_is_extra(node)) {
        return;
    }
    uint32_t count = ts_node_child_count(node);
    if (!count) {
        edit(s, node, "");
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        remove_tokens(s, ts_node_child(node, i));
    }
}
static bool contains(TSNode scope, TSNode node)
{
    return !ts_node_is_null(scope) &&
           ts_node_start_byte(scope) <= ts_node_start_byte(node) &&
           ts_node_end_byte(scope) >= ts_node_end_byte(node);
}
static bool same_name(const char *text, TSNode a, TSNode b)
{
    if (ts_node_is_null(a) || ts_node_is_null(b)) {
        return false;
    }
    uint32_t start_a = ts_node_start_byte(a), end_a = ts_node_end_byte(a);
    uint32_t start_b = ts_node_start_byte(b), end_b = ts_node_end_byte(b);
    if (is(a, "string")) {
        start_a++, end_a--;
    }
    if (is(b, "string")) {
        start_b++, end_b--;
    }
    return end_a - start_a == end_b - start_b &&
           !memcmp(text + start_a, text + start_b, end_a - start_a);
}
static bool imported_name(query_rewrite *s, TSNode name)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(s->root); i++) {
        TSNode node = ts_node_named_child(s->root, i);
        if (!is(node, "import_declaration")) {
            continue;
        }
        TSNode alias = field(node, "alias");
        if (!ts_node_is_null(alias)) {
            if (same_name(s->text, alias, name)) {
                return true;
            }
            continue;
        }
        TSNode path = field(node, "path");
        uint32_t begin = ts_node_start_byte(path) + 1,
                 end = ts_node_end_byte(path) - 1;
        for (uint32_t j = begin; j < end; j++) {
            if (s->text[j] == '/' || s->text[j] == ':') {
                begin = j + 1;
            }
        }
        uint32_t length = ts_node_end_byte(name) - ts_node_start_byte(name);
        if (end - begin == length &&
            !memcmp(s->text + begin, s->text + ts_node_start_byte(name), length)) {
            return true;
        }
    }
    return false;
}
static TSNode component_name(TSNode type)
{
    TSNode name = field(type, "name");
    if (!ts_node_is_null(name)) {
        return name;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(type); i++) {
        TSNode child = ts_node_named_child(type, i);
        if (is(child, "pointer_type")) {
            return component_name(field(child, "target"));
        }
    }
    return (TSNode){ 0 };
}
/* Before version 38 an otherwise unresolved R.T component type meant T from
 * relationship R. Imported qualified types keep their namespace. Without a
 * catalog the source-only step refuses longer ambiguous paths. */
static bool provider_type(query_rewrite *s, TSNode parameter, TSNode type)
{
    TSNode name = component_name(type), provider = { 0 }, dot = { 0 };
    int count = 0;
    for (uint32_t i = 0; !ts_node_is_null(name) && i < ts_node_child_count(name); i++) {
        TSNode child = ts_node_child(name, i);
        if (is(child, ".")) {
            dot = child;
        } else if (!ts_node_is_extra(child) && ts_node_is_named(child)) {
            if (!count) {
                provider = child;
            }
            count++;
        }
    }
    if (count < 2 || imported_name(s, provider)) {
        return false;
    }
    if (count != 2) {
        refuse(s, type, "ambiguous qualified component/provider path; spell its component type and from source explicitly before migrating");
        return false;
    }
    if (!ts_node_is_null(field(parameter, "source"))) {
        refuse(s, parameter, "compact component provider and from source are both present; choose one source before migrating");
        return false;
    }
    uint32_t length = ts_node_end_byte(provider) - ts_node_start_byte(provider);
    char *source = xmalloc((size_t)length + sizeof " from ");
    snprintf(
        source,
        (size_t)length + sizeof " from ",
        " from %.*s",
        (int)length,
        s->text + ts_node_start_byte(provider)
    );
    apush(s->owned, source);
    insert(s, ts_node_end_byte(type), source);
    bool pointer = s->text[ts_node_start_byte(type)] == '^';
    bool starts_with_provider = ts_node_start_byte(type) ==
                                ts_node_start_byte(provider);
    edit(s, provider, !pointer && starts_with_provider ? "^" : "");
    if (!pointer && !starts_with_provider) {
        insert(s, ts_node_start_byte(type), "^");
    }
    edit(s, dot, "");
    return true;
}
static bool lists_name(const char *text, TSNode list, TSNode name)
{
    for (uint32_t i = 0; !ts_node_is_null(list) && i < ts_node_named_child_count(list);
         i++) {
        TSNode node = ts_node_named_child(list, i);
        if (same_name(text, field(node, "name"), name)) {
            return true;
        }
    }
    return false;
}
static bool pattern_binds(const char *text, TSNode node, TSNode name)
{
    if (ts_node_is_null(node)) {
        return false;
    }
    if (is(node, "is_expression") && same_name(text, field(node, "binding"), name)) {
        return true;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if ((is(node, "pattern_bindings") && same_name(text, child, name)) ||
            pattern_binds(text, child, name)) {
            return true;
        }
    }
    return false;
}
static bool shadowed(query_rewrite *s, TSNode node, TSNode system, TSNode name)
{
    for (TSNode scope = ts_node_parent(node);
         !ts_node_is_null(scope) && !ts_node_eq(scope, system);
         scope = ts_node_parent(scope)) {
        if (lists_name(s->text, field(scope, "parameters"), name) ||
            lists_name(s->text, field(scope, "payload"), name) ||
            lists_name(s->text, field(scope, "results"), name)) {
            return true;
        }
        if (is(scope, "for_statement") && contains(field(scope, "body"), node) &&
            (same_name(s->text, field(scope, "name"), name) ||
             same_name(s->text, field(scope, "key"), name))) {
            return true;
        }
        if ((is(scope, "if_statement") && contains(field(scope, "consequence"), node) &&
             pattern_binds(s->text, field(scope, "condition"), name)) ||
            (is(scope, "switch_arm") &&
             pattern_binds(s->text, field(scope, "value"), name))) {
            return true;
        }
        bool block = is(scope, "block") || is(scope, "callable_body") ||
                     is(scope, "declaration_body") || is(scope, "system_body");
        for (uint32_t i = 0; block && i < ts_node_named_child_count(scope); i++) {
            TSNode declaration = ts_node_named_child(scope, i);
            bool local = is(declaration, "local_declaration");
            bool binding = local || is(declaration, "constant_declaration") ||
                           is(declaration, "state_declaration") ||
                           is(declaration, "function_declaration") ||
                           is(declaration, "macro_declaration") ||
                           is(declaration, "action_declaration");
            if (binding &&
                (!local || ts_node_end_byte(declaration) <= ts_node_start_byte(node)) &&
                same_name(s->text, field(declaration, "name"), name)) {
                return true;
            }
        }
    }
    return false;
}
static bool token_is(query_rewrite *s, TSNode node, const char *text)
{
    size_t length = strlen(text);
    return !ts_node_is_null(node) &&
           ts_node_end_byte(node) - ts_node_start_byte(node) == length &&
           !memcmp(s->text + ts_node_start_byte(node), text, length);
}
/* Parentheses do not change whether a name is a receiver or presence test. */
static TSNode enclosing_expression(TSNode node)
{
    TSNode up = ts_node_parent(node);
    while (is(up, "parenthesized_expression")) {
        node = up;
        up = ts_node_parent(node);
    }
    return node;
}
static bool pointer_use(query_rewrite *s, TSNode node, TSNode system)
{
    TSNode expression = enclosing_expression(node),
           parent = ts_node_parent(expression);
    if (is(parent, "member_expression") &&
        ts_node_eq(field(parent, "object"), expression)) {
        return true;
    }
    if (is(parent, "unary_expression")) {
        TSNode op = field(parent, "operator");
        return token_is(s, op, "!") || token_is(s, op, "not");
    }
    if (is(parent, "binary_expression")) {
        TSNode op = field(parent, "operator");
        if (token_is(s, op, "&&") || token_is(s, op, "||") ||
            token_is(s, op, "and") || token_is(s, op, "or")) {
            return true;
        }
        if ((token_is(s, op, "==") || token_is(s, op, "!=")) &&
            (is(field(parent, "left"), "nil") || is(field(parent, "right"), "nil"))) {
            return true;
        }
    }
    if (ts_node_eq(field(parent, "condition"), expression) ||
        ts_node_eq(field(system, "where"), expression)) {
        return true;
    }
    if (is(parent, "argument_list")) {
        TSNode call = ts_node_parent(parent);
        return is(call, "call_expression") &&
               named(s->text, field(call, "function"), "changed");
    }
    return false;
}
static bool read_child(TSNode node, uint32_t i)
{
    TSNode child = ts_node_child(node, i);
    if (!ts_node_is_named(child) || ts_node_is_extra(child)) {
        return false;
    }
    const char *role = ts_node_field_name_for_child(node, i);
    if (!role) {
        return true;
    }
    static const char *const names[] = {
        "name",  "member",     "type",      "type_arguments", "unit",
        "kind",  "visibility", "binding",   "bindings",       "event",
        "alias", "path",       "attribute", "storage",
    };
    for (size_t k = 0; k < sizeof names / sizeof *names; k++) {
        if (!strcmp(role, names[k])) {
            return false;
        }
    }
    if (!strcmp(role, "key")) {
        return !is(node, "for_statement");
    }
    return strcmp(role, "function") || !is(child, "identifier");
}
static void component_reads(query_rewrite *s, TSNode node, TSNode system, component_binding *components)
{
    if (ts_node_is_null(node)) {
        return;
    }
    if (is(node, "identifier")) {
        for (int i = 0; i < acount(components); i++) {
            if (!same_name(s->text, node, components[i].name) ||
                shadowed(s, node, system, components[i].name) ||
                pointer_use(s, node, system)) {
                continue;
            }
            if (components[i].optional) {
                refuse(s, node, "optional component used as a whole value; make its presence and value copy explicit before migrating");
                return;
            }
            /* The binding is already the component's address. */
            TSNode parent = ts_node_parent(enclosing_expression(node));
            if (is(parent, "address_expression")) {
                edit(s, token(parent, "&"), "");
                return;
            }
            insert(s, ts_node_end_byte(node), "^");
            return;
        }
        return;
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        if (read_child(node, i)) {
            component_reads(s, ts_node_child(node, i), system, components);
        }
    }
}
static bool bindings(query_rewrite *s, TSNode list, component_binding **components)
{
    bool any = false;
    for (uint32_t i = 0; i < ts_node_named_child_count(list); i++) {
        TSNode node = ts_node_named_child(list, i);
        if (!is(node, "parameter")) {
            continue;
        }
        any = true;
        TSNode type = field(node, "type");
        if (ts_node_is_null(type)) {
            refuse(s, node, "untyped component binding; specify its component type before migrating");
            continue;
        }
        if (!ts_node_is_null(field(node, "value"))) {
            refuse(s, node, "component binding has a default value; remove it before migrating");
            continue;
        }
        uint32_t start = ts_node_start_byte(type);
        TSNode split = token(node, "split");
        if (!ts_node_is_null(split)) {
            edit(s, split, "from");
        }
        bool provider = provider_type(s, node, type);
        if (s->text[start] != '^') {
            if (!provider) {
                insert(s, start, "^");
            }
            bool optional = s->text[ts_node_end_byte(type) - 1] == '?';
            apush(*components, ((component_binding){ field(node, "name"), optional }));
        }
    }
    return any;
}
static bool trailing_comma(TSNode list)
{
    for (uint32_t i = ts_node_child_count(list); i > 0; i--) {
        TSNode child = ts_node_child(list, i - 1);
        if (ts_node_is_extra(child) || is(child, ")")) {
            continue;
        }
        return is(child, ",");
    }
    return false;
}
static void system_header(query_rewrite *s, TSNode node)
{
    TSNode parameters = field(node, "parameters"), last = { 0 };
    component_binding *components = NULL;
    bool any = bindings(s, parameters, &components),
         separated = trailing_comma(parameters);
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode clause = ts_node_named_child(node, i);
        if (is(clause, "query_join")) {
            edit(s, token(clause, "join"), any && !separated ? "," : "");
            TSNode list = field(clause, "parameters");
            if (!ts_node_is_null(list)) {
                bindings(s, list, &components);
            }
            any = true;
            separated = !ts_node_is_null(list) && trailing_comma(list);
            last = clause;
        } else if (is(clause, "without_clause")) {
            edit(s, token(clause, "without"), any && !separated ? ", if !" : "if !");
            for (uint32_t j = 0; j < ts_node_child_count(clause); j++) {
                TSNode child = ts_node_child(clause, j);
                if (is(child, ",")) {
                    edit(
                        s,
                        child,
                        trailing_comma(clause) && ts_node_end_byte(child) ==
                                                      ts_node_end_byte(clause)
                            ? ""
                            : " && !"
                    );
                }
            }
            any = true;
            separated = false;
            last = clause;
        }
    }
    TSNode condition = field(node, "where");
    if (!ts_node_is_null(condition)) {
        edit(s, token(node, "where"), any && !separated ? ", if" : "if");
        last = condition;
    }
    component_reads(s, condition, node, components);
    component_reads(s, field(node, "body"), node, components);
    afree(components);
    if (!ts_node_is_null(last)) {
        edit(s, token(parameters, ")"), "");
        insert(s, ts_node_end_byte(last), ")");
    }
    TSNode event = field(node, "event");
    if (ts_node_is_null(event)) {
        return;
    }
    if (!named(s->text, event, "tick")) {
        refuse(s, event, "system payload is not Tick; migrate it to a handler by hand");
        return;
    }
    TSNode payload = field(node, "payload");
    for (uint32_t i = 0; i < ts_node_named_child_count(payload); i++) {
        TSNode p = ts_node_named_child(payload, i);
        if (is(p, "parameter") && !named(s->text, field(p, "name"), "dt")) {
            refuse(s, p, "Tick system payload binds a field other than dt; correct it before migrating");
            return;
        }
    }
    remove_tokens(s, token(node, "on"));
    remove_tokens(s, event);
    remove_tokens(s, field(node, "binding"));
    remove_tokens(s, payload);
}
static void rewrite(query_rewrite *s, TSNode node)
{
    if (is(node, "query_declaration")) {
        refuse(s, node, "removed query declaration; move its membership condition into the system query list as an if entry");
    }
    if (is(node, "system")) {
        system_header(s, node);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        rewrite(s, ts_node_named_child(node, i));
    }
}
/* A partially upgraded source can already use query-list guards or bindings.
 * Only remove its remaining compact provider paths: value bindings in that
 * current syntax already have the new snapshot meaning. */
static void current_providers(query_rewrite *s, TSNode node)
{
    if (is(node, "query_list")) {
        for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
            TSNode parameter = ts_node_named_child(node, i);
            if (!is(parameter, "parameter")) {
                continue;
            }
            TSNode type = field(parameter, "type");
            provider_type(s, parameter, type);
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        current_providers(s, ts_node_named_child(node, i));
    }
}

int chibi_migrate37_queries(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *syntax = chibi_migrate_parse(source, 37);
    bool current = false;
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        syntax = script_parse(source);
        if (syntax->diagnostic_count) {
            script_syntax_free(syntax);
            snprintf(error, size, "Cannot migrate invalid version 37 source");
            return -1;
        }
        current = true;
    }
    query_rewrite s = { .text = source, .root = ts_tree_root_node(syntax->tree) };
    if (current) {
        current_providers(&s, s.root);
    } else {
        rewrite(&s, s.root);
    }
    int status = 0;
    if (s.reason) {
        script_range range = script_node_range(s.refused);
        snprintf(error, size, "Query migration at %d:%d: %s", range.line, range.column, s.reason);
        status = -1;
    } else if (acount(s.edits)) {
        char *result = text_edits_apply(source, s.edits, acount(s.edits));
        script_syntax *check = script_parse(result);
        if (check->diagnostic_count) {
            script_range range = check->diagnostics[0].range;
            snprintf(
                error,
                size,
                "Query migration produced invalid source at %d:%d",
                range.line,
                range.column
            );
            free(result);
            status = -1;
        } else {
            *migrated = result;
        }
        script_syntax_free(check);
    }
    afree(s.edits);
    for (int i = 0; i < acount(s.owned); i++) {
        free(s.owned[i]);
    }
    afree(s.owned);
    script_syntax_free(syntax);
    return status;
}
