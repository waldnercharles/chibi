#include "authoring/source_edit_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static bool is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), type);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static bool signature(const char *text)
{
    const char *prefix = "__Edit :: library { __Signature :: ";
    const char *suffix = " {} }";
    size_t bytes = strlen(prefix) + strlen(text) + strlen(suffix) + 1;
    char *source = xmalloc(bytes);
    snprintf(source, bytes, "%s%s%s", prefix, text, suffix);
    script_syntax *syntax = script_parse(source);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        free(source);
        return false;
    }
    TSNode root = ts_tree_root_node(syntax->tree);
    TSNode program = ts_node_named_child(root, 0);
    TSNode declaration = ts_node_named_child(field(program, "body"), 0);
    TSNode body = field(declaration, "body");
    /* The candidate must be one header, without injected body/declarations. */
    bool valid = !syntax->diagnostic_count &&
                 is(declaration, "function_declaration") &&
                 (int)ts_node_start_byte(body) ==
                     (int)(strlen(prefix) + strlen(text) + 1) &&
                 (int)ts_node_end_byte(body) ==
                     (int)(strlen(prefix) + strlen(text) + 3);
    script_syntax_free(syntax);
    free(source);
    return valid;
}
bool source_edit_signature(
    const script_analysis *a,
    const script_catalog *catalog,
    const authoring_source_presentation *p,
    const authoring_edit_request *r,
    source_edit_changes *changes,
    char *error,
    size_t size
)
{
    if (!r->signature || strlen(r->signature) > 64 * 1024 || !signature(r->signature)) {
        return fail(error, size, "Provide one callable signature within 64 KiB: (parameters) or (parameters) -> (results)");
    }
    const source_presentation_origin *o = NULL;
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin
            *candidate = authoring_source_presentation_origin_at(p, i);
        if (candidate->identity == r->origin) {
            o = candidate;
            break;
        }
    }
    if (!o) {
        return fail(error, size, "Select an authored function or macro signature");
    }
    script_syntax *syntax = script_parse(a->text);
    TSNode node = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(syntax->tree),
        (uint32_t)o->range.start,
        (uint32_t)o->range.end
    );
    while (!ts_node_is_null(node) && !is(node, "function_declaration") &&
           !is(node, "macro_declaration")) {
        node = ts_node_parent(node);
    }
    if (ts_node_is_null(node)) {
        script_syntax_free(syntax);
        return fail(error, size, "This source record has no callable signature");
    }
    TSNode parameters = field(node, "parameters"), results = field(node, "results");
    if (ts_node_is_null(parameters)) {
        script_syntax_free(syntax);
        return fail(error, size, "The callable signature is incomplete");
    }
    TSNode last = ts_node_is_null(results) ? parameters : results;
    apush(
        changes->patches,
        ((text_edit){ (int)ts_node_start_byte(parameters),
                      (int)ts_node_end_byte(last),
                      xstrdup(r->signature) })
    );
    changes->text = text_edits_apply(a->text, changes->patches, acount(changes->patches));
    script_range body_range = script_node_range(field(node, "body"));
    script_syntax_free(syntax);
    if (!changes->text) {
        source_edit_changes_free(changes);
        return fail(error, size, "Signature edits overlap");
    }
    script_syntax *next_syntax = script_parse(changes->text);
    script_analysis *next = script_analyze_document(next_syntax, catalog, a->identity);
    int delta = (int)strlen(r->signature) -
                (changes->patches[0].end - changes->patches[0].start);
    bool valid = next->valid;
    for (int i = 0; valid && i < a->use_count; i++) {
        const script_use *old = &a->uses[i];
        if (old->symbol < 0 || old->range.start < body_range.start ||
            old->range.end > body_range.end) {
            continue;
        }
        const script_use *use = script_use_at(next, old->range.start + delta);
        valid = use && use->symbol >= 0 &&
                a->symbols[old->symbol].id == next->symbols[use->symbol].id;
    }
    script_analysis_free(next);
    script_syntax_free(next_syntax);
    if (!valid) {
        source_edit_changes_free(changes);
        return fail(error, size, "Signature changes would invalidate or capture a body reference");
    }
    return true;
}
