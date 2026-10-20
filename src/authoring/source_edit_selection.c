#include "authoring/source_edit_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_SELECTION = 256,
    MAX_FRAGMENT = 256 * 1024
};
typedef struct
{
    script_range range;
    TSNode node, region;
} selected_region;
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
static bool is(TSNode node, const char *name)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), name);
}
static TSNode program_body(const script_syntax *syntax)
{
    TSNode root = ts_tree_root_node(syntax->tree);
    for (uint32_t i = 0; i < ts_node_named_child_count(root); i++) {
        TSNode node = ts_node_named_child(root, i);
        if (is(node, "behavior") || is(node, "library") || is(node, "system")) {
            return ts_node_child_by_field_name(node, "body", 4);
        }
    }
    return (TSNode){ 0 };
}
static TSNode at(const script_syntax *syntax, script_range range)
{
    return ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(syntax->tree),
        (uint32_t)range.start,
        (uint32_t)range.end
    );
}
static bool unit(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    return is(parent, "block") || is(parent, "callable_body") ||
           is(parent, "declaration_body") || is(parent, "system_body");
}
static bool selection(
    const authoring_source_presentation *p,
    const script_syntax *syntax,
    const uint64_t *ids,
    int count,
    selected_region **out,
    char *error,
    size_t size
)
{
    if (!ids || count <= 0 || count > MAX_SELECTION) {
        return fail(error, size, "Select at most 256 complete source records");
    }
    for (int i = 0; i < count; i++) {
        const source_presentation_origin
            *o = source_edit_statement_origin(p, origin(p, ids[i]));
        if (!o || o->range.start < 0 || o->range.end <= o->range.start) {
            return fail(error, size, "Selection has no editable lexical record");
        }
        if (o->pin && !o->output) {
            return fail(error, size, "Disconnect a data input through its call argument before deleting records");
        }
        TSNode node = at(syntax, o->range);
        while (!ts_node_is_null(node) && !unit(node)) {
            node = ts_node_parent(node);
        }
        if (ts_node_is_null(node)) {
            return fail(error, size, "Select a whole statement or declaration");
        }
        selected_region record = {
            .node = node,
            .region = ts_node_parent(node),
            .range = source_edit_whole_statement(syntax, script_node_range(node))
        };
        bool contained = false;
        for (int j = 0; j < acount(*out); j++) {
            selected_region old = (*out)[j];
            if (record.range.start >= old.range.start &&
                record.range.end <= old.range.end) {
                contained = true;
                break;
            }
            if (old.range.start >= record.range.start &&
                old.range.end <= record.range.end) {
                adel(*out, j);
                j--;
            }
        }
        if (!contained) {
            apush(*out, record);
        }
    }
    for (int i = 1; i < acount(*out); i++) {
        selected_region value = (*out)[i];
        int j = i;
        while (j && (*out)[j - 1].range.start > value.range.start) {
            (*out)[j] = (*out)[j - 1];
            j--;
        }
        (*out)[j] = value;
    }
    for (int i = 0; i < acount(*out); i++) {
        if (!ts_node_eq((*out)[0].region, (*out)[i].region)) {
            return fail(error, size, "Select records within one lexical body");
        }
        if (i && (*out)[i - 1].range.end > (*out)[i].range.start) {
            return fail(error, size, "Attached comments overlap selected records");
        }
    }
    return true;
}
bool source_edit_selection_region(
    const authoring_source_presentation *p,
    const script_syntax *syntax,
    const uint64_t *ids,
    int count,
    script_range *range,
    TSNode *region,
    char *error,
    size_t size
)
{
    selected_region *records = NULL;
    bool valid = selection(p, syntax, ids, count, &records, error, size);
    for (int i = 1; valid && i < acount(records); i++) {
        TSNode next = ts_node_next_named_sibling(records[i - 1].node);
        while (is(next, "comment")) {
            next = ts_node_next_named_sibling(next);
        }
        valid = ts_node_eq(next, records[i].node);
    }
    if (valid) {
        *range = (script_range){ .start = records[0].range.start,
                                 .end = records[acount(records) - 1].range.end };
        *region = records[0].region;
    } else if (acount(records)) {
        fail(error, size, "Select contiguous complete records from one lexical body");
    }
    afree(records);
    return valid;
}
static char *copy_regions(const char *text, const selected_region *records)
{
    size_t bytes = 1;
    for (int i = 0; i < acount(records); i++) {
        bytes += (size_t)(records[i].range.end - records[i].range.start) + 1;
    }
    char *out = xmalloc(bytes), *next = out;
    for (int i = 0; i < acount(records); i++) {
        script_range r = records[i].range;
        size_t n = (size_t)(r.end - r.start);
        memcpy(next, text + r.start, n);
        next += n;
        if (!n || next[-1] != '\n') {
            *next++ = '\n';
        }
    }
    *next = 0;
    return out;
}
char *authoring_source_clipboard(
    const authoring_source_presentation *p,
    const uint64_t *ids,
    int count,
    char *error,
    size_t size
)
{
    assert(p);
    script_syntax *syntax = script_parse(authoring_source_presentation_text(p));
    selected_region *records = NULL;
    char *text = selection(p, syntax, ids, count, &records, error, size)
                   ? copy_regions(syntax->text, records)
                   : NULL;
    afree(records);
    script_syntax_free(syntax);
    if (text && size) {
        error[0] = 0;
    }
    return text;
}
static void patch(source_edit_changes *change, int start, int end, const char *text)
{
    apush(change->patches, ((text_edit){ start, end, xstrdup(text) }));
}
void source_edit_changes_free(source_edit_changes *change)
{
    free(change->text);
    for (int i = 0; i < acount(change->patches); i++) {
        free((char *)change->patches[i].text);
    }
    afree(change->patches);
    *change = (source_edit_changes){ 0 };
}
static TSNode body(const script_syntax *syntax, const source_presentation_origin *o)
{
    TSNode node = at(syntax, o->range);
    while (!ts_node_is_null(node)) {
        if (is(node, "block") || is(node, "callable_body") ||
            is(node, "declaration_body") || is(node, "system_body")) {
            return node;
        }
        TSNode block = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(block)) {
            return block;
        }
        TSNode consequence = ts_node_child_by_field_name(node, "consequence", 11);
        if (!ts_node_is_null(consequence)) {
            return consequence;
        }
        node = ts_node_parent(node);
    }
    return node;
}
static bool fragment_syntax(const char *fragment, const char *region)
{
    const char *prefix = !strcmp(region, "system_body")
                           ? "__Edit :: system() {\n"
                       : !strcmp(region, "declaration_body")
                           ? "__Edit :: behavior {\n"
                       : !strcmp(region, "callable_body")
                           ? "__Edit :: behavior { __Scope :: () {\n"
                           : "__Edit :: behavior { on Tick :: () {\n";
    const char *suffix = (!strcmp(region, "system_body") ||
                          !strcmp(region, "declaration_body"))
                           ? "\n}"
                           : "\n} }";
    size_t bytes = strlen(prefix) + strlen(fragment) + strlen(suffix) + 1;
    char *text = xmalloc(bytes);
    snprintf(text, bytes, "%s%s%s", prefix, fragment, suffix);
    script_syntax *syntax = script_parse(text);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        free(text);
        return false;
    }
    TSNode block = program_body(syntax);
    if (strcmp(region, "system_body") && strcmp(region, "declaration_body")) {
        TSNode declaration = ts_node_named_child(block, 0);
        block = ts_node_child_by_field_name(declaration, "body", 4);
    }
    bool valid = !syntax->diagnostic_count && !ts_node_is_null(block) &&
                 (size_t)ts_node_start_byte(block) == strlen(prefix) - 2 &&
                 (size_t)ts_node_end_byte(block) ==
                     strlen(prefix) + strlen(fragment) + 2;
    script_syntax_free(syntax);
    free(text);
    return valid;
}
static bool insert_fragment(
    source_edit_changes *change,
    int start,
    int end,
    const char *fragment,
    char *error,
    size_t size
)
{
    if (!fragment || strlen(fragment) > MAX_FRAGMENT) {
        return fail(error, size, "Provide a source fragment within 256 KiB");
    }
    size_t bytes = strlen(fragment) + 3;
    char *text = xmalloc(bytes);
    snprintf(text, bytes, "\n%s\n", fragment);
    patch(change, start, end, text);
    free(text);
    return true;
}
static char *renamed_copy(const script_analysis *a, const selected_region *records, const char *text)
{
    text_edit *renames = NULL;
    for (int i = 0; i < a->symbol_count; i++) {
        const script_symbol *s = &a->symbols[i];
        int first = -1;
        for (int j = 0; j < acount(records); j++) {
            if (s->declaration.start >= records[j].range.start &&
                s->declaration.end <= records[j].range.end) {
                first = j;
                break;
            }
        }
        if (first < 0) {
            continue;
        }
        char name[64];
        for (int suffix = 1;; suffix++) {
            snprintf(name, sizeof name, "copy_%llx_%d", (unsigned long long)s->id, suffix);
            bool collision = false;
            for (int j = 0; j < a->symbol_count; j++) {
                collision |= !strcmp(a->symbols[j].name, name);
            }
            for (int j = 0; j < acount(renames); j++) {
                collision |= !strcmp(renames[j].text, name);
            }
            if (!collision) {
                break;
            }
        }
        int base = 0;
        for (int j = 0; j < first; j++) {
            base += records[j].range.end - records[j].range.start;
            if (a->text[records[j].range.end - 1] != '\n') {
                base++;
            }
        }
        apush(
            renames,
            ((text_edit){ base + s->declaration.start - records[first].range.start,
                          base + s->declaration.end - records[first].range.start,
                          xstrdup(name) })
        );
        for (int u = 0; u < a->use_count; u++) {
            const script_use *use = &a->uses[u];
            if (use->symbol != i || (use->range.start == s->declaration.start &&
                                     use->range.end == s->declaration.end)) {
                continue;
            }
            base = 0;
            for (int j = 0; j < acount(records); j++) {
                if (use->range.start >= records[j].range.start &&
                    use->range.end <= records[j].range.end) {
                    apush(
                        renames,
                        ((text_edit){ base + use->range.start - records[j].range.start,
                                      base + use->range.end - records[j].range.start,
                                      xstrdup(name) })
                    );
                    break;
                }
                base += records[j].range.end - records[j].range.start;
                if (a->text[records[j].range.end - 1] != '\n') {
                    base++;
                }
            }
        }
    }
    char *out = text_edits_apply(text, renames, acount(renames));
    for (int i = 0; i < acount(renames); i++) {
        free((char *)renames[i].text);
    }
    afree(renames);
    return out;
}
bool source_edit_structural(
    const script_analysis *a,
    const script_catalog *catalog,
    const authoring_source_presentation *p,
    const authoring_edit_request *r,
    source_edit_changes *change,
    char *error,
    size_t size
)
{
    script_syntax *syntax = script_parse(a->text);
    const source_presentation_origin *o = origin(p, r->origin);
    selected_region *records = NULL;
    bool ok = false;
    if (r->kind == AUTHORING_EDIT_APPEND_BODY || r->kind == AUTHORING_EDIT_REPLACE_BODY ||
        r->kind == AUTHORING_EDIT_APPEND_DECLARATIONS ||
        r->kind == AUTHORING_EDIT_INSERT_BEFORE || r->kind == AUTHORING_EDIT_INSERT_AFTER ||
        r->kind == AUTHORING_EDIT_REPLACE_DECLARATION) {
        TSNode region = { 0 };
        if (r->kind == AUTHORING_EDIT_APPEND_DECLARATIONS) {
            region = program_body(syntax);
        } else if (o) {
            if (r->kind == AUTHORING_EDIT_APPEND_BODY ||
                r->kind == AUTHORING_EDIT_REPLACE_BODY) {
                region = body(syntax, o);
            } else {
                TSNode node = at(syntax, o->range);
                while (!ts_node_is_null(node) && !unit(node)) {
                    node = ts_node_parent(node);
                }
                region = ts_node_parent(node);
            }
        }
        const char *kind = is(region, "system_body")      ? "system_body"
                         : is(region, "declaration_body") ? "declaration_body"
                         : is(region, "callable_body")    ? "callable_body"
                                                          : "block";
        if (!r->fragment || strlen(r->fragment) > MAX_FRAGMENT ||
            ts_node_is_null(region) || !fragment_syntax(r->fragment, kind)) {
            script_syntax_free(syntax);
            return fail(error, size, "Paste or create complete records within the selected lexical body");
        }
    }
    switch (r->kind) {
        case AUTHORING_EDIT_DELETE_SELECTION:
        case AUTHORING_EDIT_DUPLICATE_SELECTION:
        case AUTHORING_EDIT_WRAP_IF:
        case AUTHORING_EDIT_WRAP_WHILE:
        case AUTHORING_EDIT_WRAP_SEQUENCE: {
            uint64_t one = r->origin;
            const uint64_t *ids = r->origin_count ? r->origins : &one;
            int count = r->origin_count ? r->origin_count : 1;
            uint64_t *units = NULL, *inputs = NULL;
            if (!ids || count <= 0 || count > MAX_SELECTION) {
                fail(error, size, "Select at most 256 complete source records");
                break;
            }
            for (int i = 0; i < count; i++) {
                const source_presentation_origin *selected = origin(p, ids[i]);
                if (r->kind == AUTHORING_EDIT_DELETE_SELECTION && selected &&
                    selected->pin && !selected->output && !selected->flow) {
                    apush(inputs, ids[i]);
                } else {
                    apush(units, ids[i]);
                }
            }
            bool valid = !acount(units) ||
                         selection(p, syntax, units, acount(units), &records, error, size);
            afree(units);
            if (valid && r->kind == AUTHORING_EDIT_DELETE_SELECTION) {
                for (int i = 0; valid && i < acount(inputs); i++) {
                    const source_presentation_origin *input = origin(p, inputs[i]);
                    bool removed = false, duplicate = false;
                    for (int j = 0; j < acount(records); j++) {
                        removed |= input->range.start >= records[j].range.start &&
                                   input->range.end <= records[j].range.end;
                    }
                    for (int j = 0; j < i; j++) {
                        duplicate |= inputs[j] == inputs[i];
                    }
                    if (!removed && !duplicate) {
                        valid = source_edit_disconnect_patch(
                            a,
                            catalog,
                            syntax,
                            p,
                            inputs[i],
                            &change->patches,
                            error,
                            size
                        );
                    }
                }
                for (int i = 0; valid && i < acount(records); i++) {
                    patch(change, records[i].range.start, records[i].range.end, "");
                }
                afree(inputs);
                ok = valid;
                break;
            }
            afree(inputs);
            if (!valid) {
                break;
            }
            char *text = copy_regions(a->text, records);
            if (r->kind == AUTHORING_EDIT_DUPLICATE_SELECTION) {
                char *copy = renamed_copy(a, records, text);
                free(text);
                if (!copy) {
                    fail(error, size, "Selected source names overlap during duplication");
                    break;
                }
                ok = insert_fragment(
                    change,
                    records[acount(records) - 1].range.end,
                    records[acount(records) - 1].range.end,
                    copy,
                    error,
                    size
                );
                free(copy);
                break;
            }
            if (!is(records[0].region, "block") &&
                !is(records[0].region, "system_body")) {
                free(text);
                fail(error, size, "Wrap executable statements within one body");
                break;
            }
            bool contiguous = true;
            for (int i = 1; i < acount(records); i++) {
                for (int at = records[i - 1].range.end; at < records[i].range.start;
                     at++) {
                    contiguous &= isspace((unsigned char)a->text[at]) != 0;
                }
            }
            if (!contiguous) {
                free(text);
                fail(error, size, "Wrap a contiguous statement selection");
                break;
            }
            if (r->kind != AUTHORING_EDIT_WRAP_SEQUENCE &&
                (!r->expression || strlen(r->expression) > MAX_FRAGMENT ||
                 !source_edit_single_expression(r->expression))) {
                free(text);
                fail(error, size, "Provide a checked control condition");
                break;
            }
            size_t bytes = strlen(text) +
                           (r->expression ? strlen(r->expression) : 0) + 48;
            char *wrapped = xmalloc(bytes);
            if (r->kind == AUTHORING_EDIT_WRAP_SEQUENCE) {
                snprintf(wrapped, bytes, "sequence { branch {\n%s} }\n", text);
            } else {
                snprintf(
                    wrapped,
                    bytes,
                    "%s (%s) {\n%s}\n",
                    r->kind == AUTHORING_EDIT_WRAP_IF ? "if" : "while",
                    r->expression,
                    text
                );
            }
            patch(
                change,
                records[0].range.start,
                records[acount(records) - 1].range.end,
                wrapped
            );
            free(wrapped);
            free(text);
            ok = true;
            break;
        }
        case AUTHORING_EDIT_APPEND_DECLARATIONS: {
            TSNode block = program_body(syntax);
            if (ts_node_is_null(block)) {
                fail(error, size, "This document has no declaration body");
                break;
            }
            int at = (int)ts_node_end_byte(block) - 1;
            ok = insert_fragment(change, at, at, r->fragment, error, size);
            break;
        }
        case AUTHORING_EDIT_APPEND_BODY:
        case AUTHORING_EDIT_REPLACE_BODY:
        case AUTHORING_EDIT_MOVE_INTO_BODY: {
            if (!o) {
                fail(error, size, "Select the destination source body");
                break;
            }
            TSNode block = body(syntax, o);
            if (!is(block, "block") && !is(block, "callable_body") &&
                !is(block, "system_body")) {
                fail(error, size, "Select an executable source body");
                break;
            }
            int first = (int)ts_node_start_byte(block) + 1,
                last = (int)ts_node_end_byte(block) - 1;
            if (r->kind != AUTHORING_EDIT_MOVE_INTO_BODY) {
                ok = insert_fragment(
                    change,
                    r->kind == AUTHORING_EDIT_REPLACE_BODY ? first : last,
                    last,
                    r->fragment,
                    error,
                    size
                );
                break;
            }
            uint64_t one = r->producer;
            if (!selection(p, syntax, &one, 1, &records, error, size)) {
                break;
            }
            script_range from = records[0].range;
            if (last >= from.start && last <= from.end) {
                fail(error, size, "A statement cannot move into its own body");
                break;
            }
            char *text = copy_regions(a->text, records);
            patch(change, from.start, from.end, "");
            ok = insert_fragment(change, last, last, text, error, size);
            change->moved = from;
            change->destination = last + 1;
            free(text);
            break;
        }
        case AUTHORING_EDIT_INSERT_BEFORE:
        case AUTHORING_EDIT_INSERT_AFTER:
        case AUTHORING_EDIT_REPLACE_DECLARATION: {
            uint64_t one = r->origin;
            if (!selection(p, syntax, &one, 1, &records, error, size)) {
                break;
            }
            script_range range = records[0].range;
            int at = r->kind == AUTHORING_EDIT_INSERT_BEFORE ? range.start
                                                             : range.end;
            ok = insert_fragment(
                change,
                r->kind == AUTHORING_EDIT_REPLACE_DECLARATION ? range.start : at,
                r->kind == AUTHORING_EDIT_REPLACE_DECLARATION ? range.end : at,
                r->fragment,
                error,
                size
            );
            break;
        }
        default:
            fail(error, size, "This structured source operation is not implemented");
            break;
    }
    if (ok) {
        change->text = text_edits_apply(a->text, change->patches, acount(change->patches));
        ok = change->text != NULL;
        if (!ok) {
            fail(error, size, "Source selection patches overlap");
        }
    }
    afree(records);
    script_syntax_free(syntax);
    if (!ok) {
        source_edit_changes_free(change);
    }
    return ok;
}
