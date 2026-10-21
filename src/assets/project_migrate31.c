#include "project_migrate31.h"

#include "project_migrate26.h"
#include "support/array.h"
#include "support/text_edit.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    const char *text;
    text_edit *edits;
    TSNode refused;
    bool extends; /* Calls `extend`, which version 31 has not. */
} array_rewrite;

static bool is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), type);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
/* Whether name spells want as the catalog matches names: without case,
 * spaces, underscores or hyphens. */
static bool named(const char *text, TSNode name, const char *want)
{
    if (ts_node_is_null(name)) {
        return false;
    }
    for (uint32_t i = ts_node_start_byte(name); i < ts_node_end_byte(name); i++) {
        char c = text[i];
        if (c == ' ' || c == '_' || c == '-') {
            continue;
        }
        if (!*want || tolower((unsigned char)c) != *want++) {
            return false;
        }
    }
    return !*want;
}
static bool array_name(const char *text, TSNode name)
{
    return named(text, name, "arrayadd") || named(text, name, "append") ||
           named(text, name, "extend");
}
static bool declaration(TSNode node)
{
    return is(node, "function_declaration") || is(node, "macro_declaration") ||
           is(node, "action_declaration") || is(node, "event_declaration") ||
           is(node, "local_declaration") || is(node, "constant_declaration") ||
           is(node, "parameter");
}
static void rewrite(array_rewrite *s, TSNode node)
{
    if (!ts_node_is_null(s->refused)) {
        return;
    }
    if (declaration(node) && array_name(s->text, field(node, "name"))) {
        s->refused = node;
        return;
    }
    TSNode callee = is(node, "call_expression") ? field(node, "function")
                                                : (TSNode){ 0 };
    bool call = is(callee, "identifier");
    s->extends |= call && named(s->text, callee, "extend");
    const char *name = !call                              ? NULL
                     : named(s->text, callee, "arrayadd") ? "append"
                     : named(s->text, callee, "append")   ? "extend"
                                                          : NULL;
    if (name) {
        apush(
            s->edits,
            ((text_edit){ (int)ts_node_start_byte(callee), (int)ts_node_end_byte(callee), name })
        );
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        rewrite(s, ts_node_named_child(node, i));
    }
}

int chibi_migrate31_array_calls(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *syntax = chibi_migrate_parse(source, 31);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        snprintf(error, size, "Cannot migrate invalid version 31 source");
        return -1;
    }
    array_rewrite s = { .text = source };
    rewrite(&s, ts_tree_root_node(syntax->tree));
    int status = 0;
    if (!ts_node_is_null(s.refused)) {
        script_range range = script_node_range(s.refused);
        snprintf(
            error,
            size,
            "declaration at %d:%d is named like an array function; version 32 "
            "calls array_add append and append extend, so rename it first",
            range.line,
            range.column
        );
        status = -1;
    } else if (acount(s.edits) && !s.extends) {
        *migrated = text_edits_apply(source, s.edits, acount(s.edits));
    }
    afree(s.edits);
    script_syntax_free(syntax);
    return status;
}
