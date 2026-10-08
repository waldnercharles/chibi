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
static const source_presentation_origin *origin(const authoring_source_presentation *p, uint64_t id)
{
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->identity == id) {
            return o;
        }
    }
    return NULL;
}
static bool is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), type);
}
static bool optional(
    const script_analysis *a,
    const script_catalog *c,
    const script_expression *call,
    int parameter
)
{
    const script_catalog_capability *cap = script_catalog_capability_by_id(c, call->capability);
    if (cap && parameter >= 0 && parameter < cap->parameter_count) {
        return cap->parameters[parameter].has_default;
    }
    if (call->right >= 0 && call->right < a->function_count) {
        const script_function *f = &a->functions[call->right];
        return parameter >= 0 && parameter < f->parameter_count &&
               a->symbols[f->parameters[parameter]].initializer >= 0;
    }
    return false;
}
/* Removing an explicit argument restores its declaration's default. Positional
 * arguments following it are left intact only when already named, so removal
 * cannot silently change parameter ordinals. */
bool source_edit_disconnect_patch(
    const script_analysis *a,
    const script_catalog *c,
    const script_syntax *syntax,
    const authoring_source_presentation *p,
    uint64_t id,
    text_edit **patches,
    char *error,
    size_t size
)
{
    const source_presentation_origin *o = origin(p, id);
    if (!o || o->kind != SOURCE_PRESENTATION_EXPRESSION || o->record < 0 ||
        o->record >= a->expression_count) {
        return fail(error, size, "Select an explicit call input to disconnect");
    }
    const script_expression *call = NULL;
    int parameter = -1;
    for (int i = 0; i < a->expression_count; i++) {
        const script_expression *e = &a->expressions[i];
        if (e->kind != SCRIPT_EXPR_CALL) {
            continue;
        }
        for (int j = 0; j < e->argument_count; j++) {
            if (e->arguments[j].value == o->record) {
                call = e;
                parameter = e->arguments[j].parameter;
            }
        }
    }
    if (!call || !optional(a, c, call, parameter)) {
        return fail(error, size, "This required input needs an explicit replacement value");
    }
    TSNode node = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(syntax->tree),
        (uint32_t)o->range.start,
        (uint32_t)o->range.end
    );
    while (!ts_node_is_null(node) && !is(ts_node_parent(node), "argument_list")) {
        node = ts_node_parent(node);
    }
    if (ts_node_is_null(node) || !is(ts_node_parent(node), "argument_list")) {
        return fail(error, size, "The default input has no explicit argument to remove");
    }
    for (TSNode next = ts_node_next_named_sibling(node); !ts_node_is_null(next);
         next = ts_node_next_named_sibling(next)) {
        if (!is(next, "named_argument") && !is(next, "typed_argument") &&
            !is(next, "comment")) {
            return fail(error, size, "Name following positional arguments before disconnecting this input");
        }
    }
    script_range range = script_node_range(node);
    TSNode next = ts_node_next_sibling(node), previous = ts_node_prev_sibling(node);
    if (is(next, ",")) {
        range.end = (int)ts_node_end_byte(next);
    } else if (is(previous, ",")) {
        range.start = (int)ts_node_start_byte(previous);
    }
    for (int i = 0; i < acount(*patches); i++) {
        text_edit *edit = &(*patches)[i];
        if (!edit->text[0] && edit->start <= range.end && edit->end >= range.start) {
            range.start = edit->start < range.start ? edit->start : range.start;
            range.end = edit->end > range.end ? edit->end : range.end;
            free((char *)edit->text);
            adel(*patches, i);
            i = -1;
        }
    }
    apush(*patches, ((text_edit){ range.start, range.end, xstrdup("") }));
    return true;
}
bool source_edit_arguments(
    const script_analysis *a,
    const script_catalog *c,
    const authoring_source_presentation *p,
    const authoring_edit_request *r,
    source_edit_changes *changes,
    char *error,
    size_t size
)
{
    script_syntax *syntax = script_parse(a->text);
    source_edit_connection value = { 0 };
    bool ok = source_edit_disconnect_patch(
        a,
        c,
        syntax,
        p,
        r->kind == AUTHORING_EDIT_RECONNECT ? r->previous : r->origin,
        &changes->patches,
        error,
        size
    );
    int destination = 0;
    const source_presentation_origin *producer = NULL;
    if (ok && r->kind == AUTHORING_EDIT_RECONNECT) {
        const source_presentation_origin *target = origin(p, r->origin);
        producer = origin(p, r->producer);
        if (!target || target->kind != SOURCE_PRESENTATION_EXPRESSION ||
            r->origin == r->previous) {
            ok = fail(error, size, "Reconnect requires a different explicit input");
        } else {
            value = source_edit_producer(a, c, p, r->producer, r->producer_result, error, size);
            ok = value.text != NULL;
            if (ok) {
                destination = target->range.start;
                for (int i = 0; i < acount(changes->patches); i++) {
                    text_edit e = changes->patches[i];
                    if (e.end <= destination) {
                        destination += (int)strlen(e.text) - (e.end - e.start);
                    }
                }
                apush(
                    changes->patches,
                    ((text_edit){ target->range.start,
                                  target->range.end,
                                  xstrdup(value.text) })
                );
            }
        }
    }
    if (ok) {
        changes
            ->text = text_edits_apply(a->text, changes->patches, acount(changes->patches));
        ok = changes->text != NULL;
        if (!ok) {
            fail(error, size, "Reconnect source ranges overlap");
        }
    }
    if (ok && value.text) {
        changes->text = source_edit_check_connection(a, c, changes->text, &value, producer, destination, error, size);
        ok = changes->text != NULL;
    }
    free(value.text);
    script_syntax_free(syntax);
    if (!ok) {
        source_edit_changes_free(changes);
    }
    return ok;
}
