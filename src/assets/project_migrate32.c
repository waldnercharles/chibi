#include "project_migrate32.h"

#include "project_migrate26.h"
#include "support/array.h"
#include "support/text_edit.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Version 32 spellings and their version 33 names. Keep the historical
 * names here: later catalog changes must not change what this step reads. */
static const char *const RENAMES[][2] = {
    { "int8", "i8" },    { "byte", "u8" },      { "int16", "i16" },
    { "uint16", "u16" }, { "uint", "u32" },     { "int64", "i64" },
    { "uint64", "u64" }, { "float", "number" }, { "double", "f64" },
};
/* Names version 33 gives the scalar types, which a version 32 source may
 * still declare for itself. */
static const char *const ADDED[] = { "i8",  "u8",  "i16", "u16", "i32",
                                     "u32", "i64", "u64", "f32", "f64" };

typedef struct
{
    const char *text;
    text_edit *edits;
    TSNode refused;
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
const char *chibi_migrate32_number_name(const char *name)
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
static bool added(const char *text, TSNode node)
{
    for (size_t i = 0; i < sizeof ADDED / sizeof *ADDED; i++) {
        if (spells(text, node, ADDED[i])) {
            return true;
        }
    }
    return false;
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
static void rewrite(number_rewrite *s, TSNode node)
{
    if (!ts_node_is_null(s->refused)) {
        return;
    }
    /* A declaration named like an old type would have its calls renamed
     * too, and one named like a new type would hide that type. */
    TSNode name = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name) && declares(node) &&
        (renamed(s->text, name) || added(s->text, name))) {
        s->refused = node;
        return;
    }
    /* A type spells its name with one identifier; a dotted name belongs to
     * a package. A conversion calls the type by name. */
    if (is(node, "type_name") && ts_node_named_child_count(node) == 1) {
        rename_at(s, node);
    }
    TSNode callee = is(node, "call_expression")
                      ? ts_node_child_by_field_name(node, "function", 8)
                      : (TSNode){ 0 };
    if (is(callee, "identifier")) {
        rename_at(s, callee);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        rewrite(s, ts_node_named_child(node, i));
    }
}

int chibi_migrate32_number_names(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *syntax = chibi_migrate_parse(source, 32);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        snprintf(error, size, "Cannot migrate invalid version 32 source");
        return -1;
    }
    number_rewrite s = { .text = source };
    rewrite(&s, ts_tree_root_node(syntax->tree));
    int status = 0;
    if (!ts_node_is_null(s.refused)) {
        script_range range = script_node_range(s.refused);
        snprintf(
            error,
            size,
            "declaration at %d:%d is named like a number type; version 33 "
            "names sized numbers i8 to u64, f32 and f64, so rename it first",
            range.line,
            range.column
        );
        status = -1;
    } else if (acount(s.edits)) {
        *migrated = text_edits_apply(source, s.edits, acount(s.edits));
    }
    afree(s.edits);
    script_syntax_free(syntax);
    return status;
}
