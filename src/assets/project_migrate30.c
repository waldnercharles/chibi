#include "project_migrate30.h"

#include "project_migrate26.h"
#include "support/array.h"
#include "support/text_edit.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What a handler or system lists to read the frame step. */
static const char STEP_FIELD[] = "dt: float s";

typedef struct
{
    const char *text;
    text_edit *edits;
    char **owned;
    TSNode *listed; /* Handlers and systems already given `dt`. */
    const char *refusal;
    TSNode refused;
} step_rewrite;

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
static uint32_t named_count(TSNode node)
{
    return ts_node_is_null(node) ? 0 : ts_node_named_child_count(node);
}
static bool spelled(const char *text, TSNode node, const char *name)
{
    if (ts_node_is_null(node)) {
        return false;
    }
    size_t length = ts_node_end_byte(node) - ts_node_start_byte(node);
    return strlen(name) == length &&
           !strncmp(text + ts_node_start_byte(node), name, length);
}
/* Whether name spells want as the catalog matches names: without case,
 * spaces, underscores or hyphens. */
static bool named(const char *text, TSNode name, const char *want)
{
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
static void edit(step_rewrite *s, uint32_t start, uint32_t end, const char *text)
{
    char *owned = xstrdup(text);
    apush(s->owned, owned);
    apush(s->edits, ((text_edit){ (int)start, (int)end, owned }));
}
static void refuse(step_rewrite *s, TSNode node, const char *reason)
{
    if (!s->refusal) {
        s->refusal = reason;
        s->refused = node;
    }
}

/* Roles whose identifiers name or select rather than read a value. */
static const char *const NAMING_ROLES[] = {
    "alias",   "attribute", "binding",        "bindings", "comptime",   "event",
    "kind",    "member",    "name",           "outcome",  "package",    "path",
    "storage", "type",      "type_arguments", "unit",     "visibility",
};
static bool read_child(TSNode node, uint32_t i)
{
    TSNode child = ts_node_child(node, i);
    const char *role = ts_node_field_name_for_child(node, i);
    if (!ts_node_is_named(child) || !role) {
        return ts_node_is_named(child);
    }
    for (size_t k = 0; k < sizeof NAMING_ROLES / sizeof *NAMING_ROLES; k++) {
        if (!strcmp(role, NAMING_ROLES[k])) {
            return false;
        }
    }
    if (!strcmp(role, "key")) {
        return !is(node, "for_statement");
    }
    return strcmp(role, "function") || !is(child, "identifier");
}
/* Whether node declares a parameter, local, constant, loop or pattern name
 * spelled like name. */
static bool declares(const char *text, TSNode node, const char *name)
{
    bool binds = is(node, "parameter") || is(node, "local_declaration") ||
                 is(node, "constant_declaration") ||
                 is(node, "for_statement") || is(node, "is_expression");
    if (binds && (spelled(text, field(node, "name"), name) ||
                  spelled(text, field(node, "key"), name) ||
                  spelled(text, field(node, "binding"), name))) {
        return true;
    }
    for (uint32_t i = 0; i < named_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if ((is(node, "pattern_bindings") && spelled(text, child, name)) ||
            declares(text, child, name)) {
            return true;
        }
    }
    return false;
}
static bool callable(TSNode node)
{
    return is(node, "handler") || is(node, "function_declaration") ||
           is(node, "macro_declaration") || is(node, "system");
}
static bool lists(const char *text, TSNode list, const char *name)
{
    for (uint32_t i = 0; i < named_count(list); i++) {
        if (spelled(text, field(ts_node_named_child(list, i), "name"), name)) {
            return true;
        }
    }
    return false;
}
/* Whether a declaration in scope at node binds name, so it reads no global:
 * a parameter of a callable around it, a loop or pattern around it, a local
 * declared before it in a block around it, or a declaration of the program. */
static bool shadowed(const char *text, TSNode node, const char *name)
{
    uint32_t at = ts_node_start_byte(node);
    for (TSNode up = ts_node_parent(node); !ts_node_is_null(up);
         up = ts_node_parent(up)) {
        if (callable(up) && (lists(text, field(up, "parameters"), name) ||
                             lists(text, field(up, "payload"), name) ||
                             lists(text, field(up, "results"), name))) {
            return true;
        }
        if (is(up, "for_statement") && (spelled(text, field(up, "name"), name) ||
                                        spelled(text, field(up, "key"), name))) {
            return true;
        }
        if ((is(up, "if_statement") && declares(text, field(up, "condition"), name)) ||
            (is(up, "switch_arm") && declares(text, field(up, "value"), name))) {
            return true;
        }
        bool block = is(up, "block") || is(up, "callable_body") ||
                     is(up, "system_body");
        for (uint32_t i = 0; block && i < named_count(up); i++) {
            TSNode child = ts_node_named_child(up, i);
            bool binds = is(child, "local_declaration") ||
                         is(child, "constant_declaration") ||
                         is(child, "state_declaration");
            if (binds && ts_node_start_byte(child) < at &&
                spelled(text, field(child, "name"), name)) {
                return true;
            }
        }
        for (uint32_t i = 0; is(up, "declaration_body") && i < named_count(up); i++) {
            TSNode child = ts_node_named_child(up, i);
            if (!is(child, "handler") && spelled(text, field(child, "name"), name)) {
                return true;
            }
        }
    }
    return false;
}
/* The declaration that evaluates node: the nearest handler, callable, action,
 * binding, state, constant or system around it. */
static TSNode evaluator(TSNode node)
{
    for (TSNode up = ts_node_parent(node); !ts_node_is_null(up);
         up = ts_node_parent(up)) {
        if (callable(up) || is(up, "action_declaration") || is(up, "bind_declaration") ||
            is(up, "state_declaration") || is(up, "constant_declaration")) {
            return up;
        }
    }
    return (TSNode){ 0 };
}
/* Adds `dt` to list once, or a whole step clause before body. */
static void list_step(step_rewrite *s, TSNode owner, TSNode list, TSNode body)
{
    for (int i = 0; i < acount(s->listed); i++) {
        if (ts_node_eq(s->listed[i], owner)) {
            return;
        }
    }
    apush(s->listed, owner);
    if (ts_node_is_null(list)) {
        char clause[64];
        snprintf(clause, sizeof clause, "on Tick :: (%s) ", STEP_FIELD);
        edit(s, ts_node_start_byte(body), ts_node_start_byte(body), clause);
        return;
    }
    bool fields = false;
    for (uint32_t i = 0; i < named_count(list); i++) {
        fields |= !ts_node_is_extra(ts_node_named_child(list, i));
    }
    char entry[64];
    snprintf(entry, sizeof entry, "%s%s", fields ? ", " : "", STEP_FIELD);
    edit(s, ts_node_end_byte(list) - 1, ts_node_end_byte(list) - 1, entry);
}
/* A read of a step global: node is the identifier, or the call whose callee
 * names the capability. */
static void step_read(step_rewrite *s, TSNode node)
{
    TSNode spelling = is(node, "call_expression") ? field(node, "function") : node;
    uint32_t length = ts_node_end_byte(spelling) - ts_node_start_byte(spelling);
    char *name = xmalloc(length + 1);
    memcpy(name, s->text + ts_node_start_byte(spelling), length);
    name[length] = '\0';
    bool local = shadowed(s->text, node, name);
    free(name);
    if (local) {
        return;
    }
    TSNode owner = evaluator(node);
    bool handler = is(owner, "handler") &&
                   spelled(s->text, field(owner, "kind"), "on") &&
                   named(s->text, field(owner, "name"), "tick");
    if (handler) {
        list_step(s, owner, field(owner, "parameters"), field(owner, "body"));
    } else if (is(owner, "system")) {
        list_step(s, owner, field(owner, "payload"), field(owner, "body"));
    } else {
        refuse(s, node, "reads the frame step outside a Tick handler or system; pass dt to it");
        return;
    }
    if (!spelled(s->text, node, "dt")) {
        edit(s, ts_node_start_byte(node), ts_node_end_byte(node), "dt");
    }
}
static void reads(step_rewrite *s, TSNode node)
{
    if (s->refusal) {
        return;
    }
    if (is(node, "member_expression")) {
        reads(s, field(node, "object"));
        return;
    }
    TSNode callee = field(node, "function");
    if (is(node, "call_expression") && is(callee, "identifier") &&
        named(s->text, callee, "deltatime") &&
        !named_count(field(node, "arguments"))) {
        step_read(s, node);
        return;
    }
    if (is(node, "identifier")) {
        if (named(s->text, node, "dt") || named(s->text, node, "delta")) {
            step_read(s, node);
        }
        return;
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        if (read_child(node, i)) {
            reads(s, ts_node_child(node, i));
        }
    }
}

int chibi_migrate30_step_globals(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *syntax = chibi_migrate_parse(source, 30);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        snprintf(error, size, "Cannot migrate invalid version 30 source");
        return -1;
    }
    step_rewrite s = { .text = source };
    reads(&s, ts_tree_root_node(syntax->tree));
    int status = 0;
    if (s.refusal) {
        script_range range = script_node_range(s.refused);
        snprintf(error, size, "step read at %d:%d %s", range.line, range.column, s.refusal);
        status = -1;
    } else if (acount(s.edits)) {
        *migrated = text_edits_apply(source, s.edits, acount(s.edits));
    }
    for (int i = 0; i < acount(s.owned); i++) {
        free(s.owned[i]);
    }
    afree(s.owned);
    afree(s.edits);
    afree(s.listed);
    script_syntax_free(syntax);
    return status;
}
