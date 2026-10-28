#include "project_migrate36.h"

#include "project_migrate26.h"
#include "support/array.h"
#include "support/text_edit.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    const char *text;
    text_edit *edits;
    uint32_t *forwarded; /* Starts of `ref` arguments that pass a parameter on. */
    TSNode refused;
    const char *reason;
} pointer_rewrite;

static bool is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), type);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_is_null(node)
             ? (TSNode){ 0 }
             : ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static bool same_text(const char *text, TSNode a, TSNode b)
{
    uint32_t length = ts_node_end_byte(a) - ts_node_start_byte(a);
    return length == ts_node_end_byte(b) - ts_node_start_byte(b) &&
           !memcmp(text + ts_node_start_byte(a), text + ts_node_start_byte(b), length);
}
static void edit(pointer_rewrite *s, uint32_t start, uint32_t end, const char *text)
{
    apush(s->edits, ((text_edit){ (int)start, (int)end, text }));
}
static void refuse(pointer_rewrite *s, TSNode node, const char *reason)
{
    if (ts_node_is_null(s->refused)) {
        s->refused = node;
        s->reason = reason;
    }
}
static bool callable(TSNode node)
{
    return is(node, "function_declaration") || is(node, "macro_declaration");
}
/* The `ref` token of a type, or a null node. */
static TSNode ref_token(TSNode type)
{
    if (is(type, "measured_type")) {
        type = field(type, "base");
    }
    for (uint32_t i = 0; is(type, "type") && i < ts_node_child_count(type); i++) {
        if (is(ts_node_child(type, i), "ref")) {
            return ts_node_child(type, i);
        }
    }
    return (TSNode){ 0 };
}
/* `ref T` becomes `^T`, and `^(T)` when T adds `[]` or `?`. */
static void rewrite_type(pointer_rewrite *s, TSNode type)
{
    bool compound = false;
    uint32_t ref = 0;
    for (uint32_t i = 0; i < ts_node_child_count(type); i++) {
        TSNode child = ts_node_child(type, i);
        const char *trait = ts_node_field_name_for_child(type, i);
        if (is(child, "ref")) {
            ref = i;
        } else if (trait && !strcmp(trait, "trait")) {
            refuse(s, type, "declares ref with another trait");
        }
        compound |= is(child, "[") || is(child, "?");
    }
    TSNode token = ts_node_child(type, ref), next = ts_node_child(type, ref + 1);
    edit(s, ts_node_start_byte(token), ts_node_start_byte(next), compound ? "^(" : "^");
    if (compound) {
        edit(s, ts_node_end_byte(type), ts_node_end_byte(type), ")");
    }
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
/* Each use of the `ref` parameter `name` in `node` reads through its
 * pointer, and passing it on forwards the pointer itself. */
static void uses(pointer_rewrite *s, TSNode node, TSNode name)
{
    if (callable(node)) {
        return; /* Nested callables cannot see enclosing parameters. */
    }
    if (!is(node, "identifier") || !same_text(s->text, node, name)) {
        for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
            uses(s, ts_node_named_child(node, i), name);
        }
        return;
    }
    TSNode parent = ts_node_parent(node);
    if (is(parent, "pattern_bindings") ||
        (binding_form(parent) && name_position(parent, node))) {
        refuse(s, node, "shadows a ref parameter; rename the local");
        return;
    }
    if (name_position(parent, node) || (is(parent, "member_expression") &&
                                        ts_node_eq(field(parent, "object"), node))) {
        return;
    }
    if (is(parent, "reference_argument")) {
        apush(s->forwarded, ts_node_start_byte(parent));
        edit(s, ts_node_start_byte(parent), ts_node_start_byte(node), "");
        return;
    }
    edit(s, ts_node_end_byte(node), ts_node_end_byte(node), "^");
}
/* A typed argument that passes a reference annotates its pointer:
 * `v: int = ref x` becomes `v: ^int = &x`. */
static void point_annotation(pointer_rewrite *s, TSNode argument)
{
    TSNode parent = ts_node_parent(argument), type = field(parent, "type");
    if (!is(parent, "typed_argument") || !ts_node_eq(field(parent, "value"), argument) ||
        !ts_node_is_null(ref_token(type))) {
        return;
    }
    bool compound = false;
    for (uint32_t i = 0; is(type, "type") && i < ts_node_child_count(type); i++) {
        compound |= is(ts_node_child(type, i), "[") || is(ts_node_child(type, i), "?");
    }
    edit(s, ts_node_start_byte(type), ts_node_start_byte(type), compound ? "^(" : "^");
    if (compound) {
        edit(s, ts_node_end_byte(type), ts_node_end_byte(type), ")");
    }
}
static bool forwarded(const pointer_rewrite *s, TSNode argument)
{
    for (int i = 0; i < acount(s->forwarded); i++) {
        if (s->forwarded[i] == ts_node_start_byte(argument)) {
            return true;
        }
    }
    return false;
}
static void rewrite(pointer_rewrite *s, TSNode node)
{
    if (is(node, "type") && !ts_node_is_null(ref_token(node))) {
        rewrite_type(s, node);
    }
    /* `for item: ref in items` becomes `for &item in items`. */
    if (is(node, "for_statement") && !ts_node_is_null(field(node, "reference"))) {
        TSNode name = field(node, "name");
        edit(s, ts_node_start_byte(name), ts_node_start_byte(name), "&");
        edit(s, ts_node_end_byte(name), ts_node_end_byte(field(node, "reference")), "");
    }
    if (is(node, "reference_argument")) {
        point_annotation(s, node);
    }
    if (is(node, "reference_argument") && !forwarded(s, node)) {
        edit(s, ts_node_start_byte(node), ts_node_start_byte(field(node, "value")), "&");
    }
    TSNode parameters = field(node, "parameters");
    for (uint32_t i = 0; callable(node) && i < ts_node_named_child_count(parameters);
         i++) {
        TSNode parameter = ts_node_named_child(parameters, i);
        if (!ts_node_is_null(ref_token(field(parameter, "type")))) {
            uses(s, field(node, "body"), field(parameter, "name"));
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        rewrite(s, ts_node_named_child(node, i));
    }
}

int chibi_migrate36_pointers(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *syntax = chibi_migrate_parse(source, 36);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        /* A retry after an interrupted upgrade reads this step's own output,
         * whose `for &item` only version 37 on spells. */
        syntax = chibi_migrate_parse(source, 37);
        bool current = !syntax->diagnostic_count;
        script_syntax_free(syntax);
        if (current) {
            return 0;
        }
        snprintf(error, size, "Cannot migrate invalid version 36 source");
        return -1;
    }
    pointer_rewrite s = { .text = source };
    rewrite(&s, ts_tree_root_node(syntax->tree));
    int status = 0;
    if (!ts_node_is_null(s.refused)) {
        script_range range = script_node_range(s.refused);
        snprintf(
            error,
            size,
            "Source at %d:%d %s, so version 37 cannot replace ref with "
            "pointers",
            range.line,
            range.column,
            s.reason
        );
        status = -1;
    } else if (acount(s.edits)) {
        *migrated = text_edits_apply(source, s.edits, acount(s.edits));
    }
    afree(s.edits);
    afree(s.forwarded);
    script_syntax_free(syntax);
    return status;
}
