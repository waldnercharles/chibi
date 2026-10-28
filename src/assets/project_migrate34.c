#include "project_migrate34.h"

#include "language/syntax.h"
#include "project_migrate26.h"
#include "support/array.h"
#include "support/text_edit.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <yyjson.h>

/* Version 34 spellings and their version 35 names. Keep the historical
 * names here: later catalog changes must not change what this step reads. */
static const char *const RENAMES[][2] = {
    { "int8", "i8" },    { "byte", "u8" },      { "int16", "i16" },
    { "uint16", "u16" }, { "uint", "u32" },     { "int64", "i64" },
    { "uint64", "u64" }, { "float", "number" }, { "double", "f64" },
};
/* Names version 35 gives the scalar types, which a version 34 source may
 * still declare for itself. */
static const char *const ADDED[] = { "i8",  "u8",  "i16", "u16", "i32",
                                     "u32", "i64", "u64", "f32", "f64" };

typedef struct
{
    const char *text;
    text_edit *edits;
    TSNode refused;
    const char *reason;
    /* The first local named `number`, which version 34 already allowed. */
    TSNode local_number;
} number_rewrite;

static bool is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), type);
}
static bool spells(const char *text, TSNode node, const char *name)
{
    size_t length = ts_node_end_byte(node) - ts_node_start_byte(node);
    return strlen(name) == length &&
           !memcmp(text + ts_node_start_byte(node), name, length);
}
const char *chibi_migrate34_number_name(const char *name)
{
    for (size_t i = 0; i < sizeof RENAMES / sizeof *RENAMES; i++) {
        if (!strcmp(name, RENAMES[i][0])) {
            return RENAMES[i][1];
        }
    }
    return NULL;
}
static const char *renamed(const char *text, TSNode node)
{
    for (size_t i = 0; i < sizeof RENAMES / sizeof *RENAMES; i++) {
        if (spells(text, node, RENAMES[i][0])) {
            return RENAMES[i][1];
        }
    }
    return NULL;
}
static bool reserved(const char *text, TSNode node)
{
    if (renamed(text, node)) {
        return true;
    }
    for (size_t i = 0; i < sizeof RENAMES / sizeof *RENAMES; i++) {
        if (spells(text, node, RENAMES[i][1])) {
            return true;
        }
    }
    for (size_t i = 0; i < sizeof ADDED / sizeof *ADDED; i++) {
        if (spells(text, node, ADDED[i])) {
            return true;
        }
    }
    return false;
}
static bool reserved_name(const char *name)
{
    for (size_t i = 0; i < sizeof RENAMES / sizeof *RENAMES; i++) {
        if (!strcmp(name, RENAMES[i][0]) || !strcmp(name, RENAMES[i][1])) {
            return true;
        }
    }
    for (size_t i = 0; i < sizeof ADDED / sizeof *ADDED; i++) {
        if (!strcmp(name, ADDED[i])) {
            return true;
        }
    }
    return false;
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static void refuse(number_rewrite *s, TSNode node, const char *reason)
{
    if (!s->reason) {
        s->refused = node;
        s->reason = reason;
    }
}
/* Whether the node's `name` field declares something a type name would
 * hide: members, arguments, variants, labels and handlers stay apart, and
 * a type's name refers to the type. */
static bool declares(TSNode node)
{
    static const char *const apart[] = {
        "record_field",        "component_field",
        "union_variant",       "variant_pattern",
        "struct_field",        "named_argument",
        "typed_argument",      "goto_statement",
        "label_statement",     "handler",
        "attribute",           "package_header",
        "prototype_component", "type",
        "union_type",
    };
    for (size_t i = 0; i < sizeof apart / sizeof *apart; i++) {
        if (is(node, apart[i])) {
            return false;
        }
    }
    return true;
}
static void rename_at(number_rewrite *s, TSNode name)
{
    const char *to = renamed(s->text, name);
    if (to) {
        apush(s->edits, ((text_edit){ (int)ts_node_start_byte(name), (int)ts_node_end_byte(name), to }));
    }
}
/* Inventory every binding before considering edits. This deliberately rejects
 * conflicting declarations even outside the use's local scope: the production
 * caller inventories every source before any file is rewritten. */
static void inventory(number_rewrite *s, TSNode node)
{
    TSNode name = field(node, "name");
    bool variant_field = is(node, "parameter") &&
                         is(ts_node_parent(ts_node_parent(node)), "union_variant");
    /* `number` keeps its meaning, so a local of that name only captures
     * the `float` spellings this step renames to it. */
    bool local_number = is(node, "local_declaration") &&
                        spells(s->text, name, "number");
    if (local_number && ts_node_is_null(s->local_number)) {
        s->local_number = name;
    }
    if (!ts_node_is_null(name) && declares(node) && !variant_field &&
        !local_number && reserved(s->text, name)) {
        refuse(s, name, "declaration is named like a number type; rename it first");
    }
    TSNode binding = is(node, "for_statement") ? field(node, "key")
                   : is(node, "is_expression") ? field(node, "binding")
                                               : (TSNode){ 0 };
    if (!ts_node_is_null(binding) && reserved(s->text, binding)) {
        refuse(s, binding, "binding is named like a number type; rename it first");
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (is(node, "pattern_bindings") && reserved(s->text, child)) {
            refuse(s, child, "binding is named like a number type; rename it first");
        }
        inventory(s, child);
    }
}
/* These fields select a separate namespace or spell a binding, not a value.
 * Type fields are visited separately, including their application arguments. */
static bool naming_role(TSNode parent, const char *role)
{
    if (!role) {
        return false;
    }
    static const char *const names[] = {
        "name",       "alias",   "binding", "bindings", "event",
        "outcome",    "unit",    "package", "path",     "attribute",
        "visibility", "storage", "kind",    "comptime", "trait",
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        if (!strcmp(role, names[i])) {
            return true;
        }
    }
    return !strcmp(role, "key") && is(parent, "for_statement");
}
static void rewrite(number_rewrite *s, TSNode node, bool argument)
{
    if (s->reason || is(node, "unit") || is(node, "argument_name") ||
        is(node, "type_binder") || is(node, "variant_pattern") ||
        is(node, "string") || is(node, "comment")) {
        return;
    }
    if (is(node, "type_name")) {
        if (ts_node_named_child_count(node) == 1) {
            rename_at(s, node);
        }
        return;
    }
    if (is(node, "identifier")) {
        if (renamed(s->text, node)) {
            if (argument) {
                rename_at(s, node);
            } else {
                refuse(s, node, "unsupported number-type expression; use an explicit type argument before migration");
            }
        }
        return;
    }
    if (is(node, "member_expression")) {
        TSNode object = field(node, "object");
        /* Preserve qualifier tokens, but do visit calls/indices within an
         * object such as pkg.Make(double).field. */
        if (!is(object, "identifier")) {
            rewrite(s, object, argument);
        }
        return;
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        const char *role = ts_node_field_name_for_child(node, i);
        /* Aliased union members inherit fields from _single_type. Their
         * type nodes are references even when the parser reports name. */
        if (is(child, "type") || is(child, "type_name") || is(child, "union_type")) {
            rewrite(s, child, false);
            continue;
        }
        if (!ts_node_is_named(child) || naming_role(node, role)) {
            continue;
        }
        bool value = argument || is(node, "argument_list");
        if (role && !strcmp(role, "function") && is(node, "call_expression") &&
            is(child, "identifier")) {
            value = true;
        }
        rewrite(s, child, value);
    }
}
static bool identifier(const char *name)
{
    for (size_t i = 0; name[i]; i++) {
        char c = name[i];
        if (c != '_' && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(i && c >= '0' && c <= '9')) {
            return false;
        }
    }
    return *name != 0;
}
static void check_imports(number_rewrite *s, TSNode node)
{
    if (s->reason || !acount(s->edits)) {
        return;
    }
    if (is(node, "import_declaration")) {
        TSNode alias = field(node, "alias");
        if (!ts_node_is_null(alias)) {
            if (reserved(s->text, alias)) {
                refuse(s, alias, "import alias is named like a number type; use a different alias before migration");
            }
            return;
        }
        TSNode path = field(node, "path");
        size_t start = ts_node_start_byte(path),
               length = ts_node_end_byte(path) - start;
        yyjson_doc *doc = yyjson_read(s->text + start, length, 0);
        yyjson_val *value = doc ? yyjson_doc_get_root(doc) : NULL;
        const char *decoded = yyjson_get_str(value);
        if (!decoded || yyjson_get_len(value) != strlen(decoded) ||
            strlen(decoded) >= 4096 || strchr(decoded, '\\') || *decoded == '/') {
            refuse(s, path, "invalid import path for number-type migration");
            yyjson_doc_free(doc);
            return;
        }
        /* Routes.ImportRoute normalizes earlier components before appending
         * this terminal identifier. Its alias is independent of the root if
         * routing succeeds. Other tails need directory/collection context. */
        const char *tail = strrchr(decoded, '/');
        if (!tail) {
            tail = strchr(decoded, ':');
        }
        tail = tail ? tail + 1 : decoded;
        if (!identifier(tail)) {
            refuse(s, path, "unresolved import alias may capture a number-type rewrite; supply an explicit alias");
        } else if (reserved_name(tail)) {
            refuse(s, path, "import alias is named like a number type; use a different alias before migration");
        }
        yyjson_doc_free(doc);
        return;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        check_imports(s, ts_node_named_child(node, i));
    }
}
static int migrate(const char *source, char **migrated, char *error, size_t size)
{
    if (migrated) {
        *migrated = NULL;
    }
    script_syntax *syntax = chibi_migrate_parse(source, 34);
    if (syntax->diagnostic_count) {
        script_range range = syntax->diagnostics[0].range;
        snprintf(
            error,
            size,
            "invalid version 34 source at %d:%d",
            range.line,
            range.column
        );
        script_syntax_free(syntax);
        return -1;
    }
    number_rewrite s = { .text = source };
    TSNode root = ts_tree_root_node(syntax->tree);
    inventory(&s, root);
    rewrite(&s, root, false);
    for (int i = 0; !ts_node_is_null(s.local_number) && i < acount(s.edits); i++) {
        if (!strcmp(s.edits[i].text, "number")) {
            refuse(&s, s.local_number, "declaration is named like a number type; rename it first");
        }
    }
    check_imports(&s, root);
    int status = 0;
    if (s.reason) {
        script_range range = script_node_range(s.refused);
        snprintf(error, size, "%s at %d:%d", s.reason, range.line, range.column);
        status = -1;
    } else if (migrated && acount(s.edits)) {
        *migrated = text_edits_apply(source, s.edits, acount(s.edits));
    }
    afree(s.edits);
    script_syntax_free(syntax);
    return status;
}
int chibi_migrate34_preflight(const char *source, char *error, size_t size)
{
    return migrate(source, NULL, error, size);
}
int chibi_migrate34_number_names(const char *source, char **migrated, char *error, size_t size)
{
    return migrate(source, migrated, error, size);
}
