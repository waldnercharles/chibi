#include "authoring/source_intelligence.h"

#include "authoring/source_literal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_ITEMS = 512,
    MAX_DETAIL = 4096
};

static bool kind(TSNode node, const char *name)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), name);
}
static TSNode field(TSNode node, const char *name);
static const script_symbol *symbol(const authoring_navigation *index, authoring_target target)
{
    for (int d = 0; d < authoring_navigation_document_count(index); d++) {
        if (target.document >= 0 && target.document != d) {
            continue;
        }
        const script_analysis *a = authoring_navigation_document_at(index, d)->analysis;
        if (!a) {
            continue;
        }
        for (int i = 0; i < a->symbol_count; i++) {
            const script_symbol *s = &a->symbols[i];
            uint64_t id = (s->kind == SCRIPT_FUNCTION || s->kind == SCRIPT_MACRO) &&
                                  s->capability
                            ? s->capability
                            : s->id;
            if (id == target.id && (target.kind == AUTHORING_SYMBOL ||
                                    target.kind == AUTHORING_CALLABLE)) {
                return s;
            }
        }
    }
    return NULL;
}
static const char *highlight(const authoring_navigation *index, authoring_target target)
{
    if (target.kind == AUTHORING_TYPE || target.kind == AUTHORING_PROTOTYPE) {
        return "tok-type";
    }
    if (target.kind == AUTHORING_FIELD) {
        return "tok-field";
    }
    if (target.kind == AUTHORING_CALLABLE) {
        return "tok-function";
    }
    if (target.kind == AUTHORING_PROGRAM) {
        return "tok-type";
    }
    const script_symbol *s = symbol(index, target);
    if (!s) {
        return "tok-ident";
    }
    switch (s->kind) {
        case SCRIPT_STATE:
        case SCRIPT_SHARED_STATE: return "tok-state";
        case SCRIPT_PARAMETER:
        case SCRIPT_RESULT: return "tok-param";
        case SCRIPT_COMPONENT:
        case SCRIPT_TYPE:
        case SCRIPT_TYPE_PARAMETER: return "tok-type";
        case SCRIPT_EVENT: return "tok-event";
        case SCRIPT_FUNCTION:
        case SCRIPT_MACRO: return "tok-function";
        case SCRIPT_LABEL: return "tok-label";
        default: return "tok-local";
    }
}
static const char *lexical(TSNode n, const char *text)
{
    if (kind(n, "comment")) {
        return "tok-comment";
    }
    if (kind(n, "string") || kind(n, "character")) {
        return "tok-string";
    }
    if (kind(n, "number")) {
        return "tok-number";
    }
    if (kind(n, "identifier")) {
        if (kind(ts_node_parent(n), "type_binder")) {
            return "tok-type";
        }
        return "tok-ident";
    }
    if (!ts_node_is_named(n) && isalpha((unsigned char)text[ts_node_start_byte(n)])) {
        return "tok-keyword";
    }
    return NULL;
}
authoring_highlight *authoring_source_syntax_highlights(const script_syntax *syntax, int *count)
{
    assert(count);
    *count = 0;
    if (!syntax) {
        return NULL;
    }
    authoring_highlight *items = NULL;
    TSTreeCursor cursor = ts_tree_cursor_new(ts_tree_root_node(syntax->tree));
    for (;;) {
        if (ts_tree_cursor_goto_first_child(&cursor)) {
            continue;
        }
        TSNode n = ts_tree_cursor_current_node(&cursor);
        if (ts_node_end_byte(n) > ts_node_start_byte(n)) {
            const char *cls = lexical(n, syntax->text);
            if (cls) {
                apush(items, ((authoring_highlight){ script_node_range(n), cls }));
            }
        }
        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                goto scanned;
            }
        }
    }
scanned:
    ts_tree_cursor_delete(&cursor);
    *count = acount(items);
    authoring_highlight *out = xmalloc((size_t)(*count + 1) * sizeof *out);
    if (*count) {
        memcpy(out, items, (size_t)*count * sizeof *out);
    }
    afree(items);
    return out;
}
authoring_highlight *authoring_source_highlights(const authoring_navigation *index, int document, int *count)
{
    assert(index && count);
    const authoring_document *doc = authoring_navigation_document_at(index, document);
    authoring_highlight *items = authoring_source_syntax_highlights(doc->syntax, count);
    for (int r = 0; r < authoring_navigation_reference_count(index); r++) {
        const authoring_reference *ref = authoring_navigation_reference_at(index, r);
        if (ref->location.document != document) {
            continue;
        }
        script_range range = ref->location.range;
        for (int i = 0; i < *count; i++) {
            if (items[i].range.start >= range.start &&
                items[i].range.end <= range.end) {
                items[i].cls = highlight(index, ref->target);
            }
        }
    }
    return items;
}
static void append(char **out, const char *text)
{
    int old = acount(*out);
    size_t length = strlen(text);
    if (!length) {
        return;
    }
    if (length > (size_t)(MAX_DETAIL - old)) {
        if (old < MAX_DETAIL) {
            static const char omitted[] = " [truncated]";
            int keep = old < MAX_DETAIL - (int)sizeof omitted + 1
                         ? old
                         : MAX_DETAIL - (int)sizeof omitted + 1;
            asetlen(*out, keep + (int)sizeof omitted - 1);
            memcpy(*out + keep, omitted, sizeof omitted - 1);
        }
        return;
    }
    asetlen(*out, old + (int)length);
    memcpy(*out + old, text, length);
}
static void type_text(char **out, const script_catalog *catalog, script_type type)
{
    if (type.flags & SCRIPT_TYPE_REFERENCE) {
        /* A scoped pointer parameter shows the `^T` its author wrote. */
        type.flags &= ~SCRIPT_TYPE_REFERENCE;
        bool compound = type.container != PIN_CONTAINER_SINGLE ||
                        (type.flags & SCRIPT_TYPE_OPTIONAL);
        append(out, compound ? "^(" : "^");
        type_text(out, catalog, type);
        append(out, compound ? ")" : "");
        return;
    }
    if (type.container == PIN_CONTAINER_MAP) {
        append(out, "[");
        type_text(out, catalog, script_catalog_key_type(catalog, type));
        append(out, ": ");
        type_text(out, catalog, script_catalog_element_type(catalog, type));
        append(out, "]");
        if (type.flags & SCRIPT_TYPE_OPTIONAL) {
            append(out, "?");
        }
        return;
    }
    if (type.container == PIN_CONTAINER_ARRAY && type.collection) {
        type_text(out, catalog, script_catalog_element_type(catalog, type));
        append(out, "[]");
        if (type.flags & SCRIPT_TYPE_OPTIONAL) {
            append(out, "?");
        }
        return;
    }
    const script_catalog_type *named = script_catalog_type_by_id(catalog, type.nominal);
    script_reference_kind reference = script_reference_kind_for_type(type);
    if (reference != SCRIPT_REFERENCE_NONE) {
        append(out, script_reference_type_name(reference));
    } else if (named && named->source_name && strchr(named->source_name, '(')) {
        append(out, named->source_name);
    } else if (named) {
        const char *name = named->source_name ? named->source_name : named->name;
        char *spelling = authoring_source_identifier(name, NULL, 0);
        append(out, spelling ? spelling : name);
        free(spelling);
    } else {
        append(out, script_numeric_name(type));
    }
    if (type.container == PIN_CONTAINER_ARRAY) {
        append(out, "[]");
    }
    if (type.container == PIN_CONTAINER_SET) {
        append(out, " set");
    }
    if (type.flags & SCRIPT_TYPE_OPTIONAL) {
        append(out, "?");
    }
}
static char *finish_text(char *text)
{
    apush(text, 0);
    char *out = xstrdup(text);
    afree(text);
    return out;
}
static char *declaration(const script_catalog *catalog, const char *name, script_type type, const char *unit)
{
    char *out = NULL;
    append(&out, name);
    append(&out, ": ");
    type_text(&out, catalog, type);
    if (unit) {
        append(&out, " ");
        append(&out, unit);
    }
    return finish_text(out);
}
static void parameter_text(char **out, const script_catalog *catalog, const script_catalog_parameter *p)
{
    append(out, p->name);
    append(out, ": ");
    type_text(out, catalog, p->type);
    if (p->unit) {
        append(out, " ");
        append(out, p->unit);
    }
    if (p->has_default) {
        append(out, " = ");
        char value[96];
        if (p->default_owner) {
            append(out, "self");
        } else if (p->default_value.type == PIN_BOOL) {
            append(out, p->default_value.b ? "true" : "false");
        } else if (p->default_value.type == PIN_I32) {
            snprintf(value, sizeof value, "%d", p->default_value.i);
            append(out, value);
        } else if (p->default_value.type == PIN_F64) {
            snprintf(value, sizeof value, "%.17g", p->default_value.d);
            append(out, value);
        } else if (p->default_value.type == PIN_F32) {
            snprintf(value, sizeof value, "%.9g", p->default_value.f);
            append(out, value);
        } else {
            append(out, "...");
        }
    }
}
static char *signature(const script_catalog *catalog, const script_catalog_capability *cap)
{
    char *out = NULL;
    append(&out, cap->qualified_name ? cap->qualified_name : cap->name);
    append(&out, "(");
    for (int i = 0; i < cap->parameter_count; i++) {
        if (i) {
            append(&out, ", ");
        }
        parameter_text(&out, catalog, &cap->parameters[i]);
    }
    append(&out, ")");
    if (cap->result_count) {
        append(&out, " -> (");
        for (int i = 0; i < cap->result_count; i++) {
            if (i) {
                append(&out, ", ");
            }
            parameter_text(&out, catalog, &cap->results[i]);
        }
        append(&out, ")");
    }
    return finish_text(out);
}
static bool specialization(const script_catalog *catalog, uint64_t id)
{
    for (int i = 0; i < catalog->specialization_count; i++) {
        if (catalog->specializations[i].id == id) {
            return true;
        }
    }
    return false;
}
static char *template_signature(const script_catalog *catalog, const script_catalog_template *definition)
{
    char *out = NULL;
    append(&out, definition->name);
    append(&out, "(");
    for (int i = 0; i < definition->parameter_count; i++) {
        const script_template_parameter *parameter = &definition->parameters[i];
        if (i) {
            append(&out, ", ");
        }
        if (parameter->comptime) {
            append(&out, "$");
        }
        append(&out, parameter->name);
        append(&out, ": ");
        if (parameter->type_value) {
            append(&out, "Type");
        } else if (parameter->binder) {
            bool pointer = parameter->type.flags & SCRIPT_TYPE_REFERENCE;
            bool compound = parameter->type.container == PIN_CONTAINER_ARRAY ||
                            (parameter->type.flags & SCRIPT_TYPE_OPTIONAL);
            append(&out, !pointer ? "" : compound ? "^(" : "^");
            append(&out, parameter->binds_type ? "$" : "");
            append(&out, parameter->binder);
            if (parameter->type.container == PIN_CONTAINER_ARRAY) {
                append(&out, "[]");
            }
            if (parameter->type.flags & SCRIPT_TYPE_OPTIONAL) {
                append(&out, "?");
            }
            append(&out, pointer && compound ? ")" : "");
        } else {
            type_text(&out, catalog, parameter->type);
        }
    }
    append(&out, ")");
    if (definition->kind == SCRIPT_TEMPLATE_RECORD) {
        append(&out, " :: struct");
    } else if (definition->kind == SCRIPT_TEMPLATE_TYPE_FUNCTION) {
        append(&out, " -> Type");
    }
    return finish_text(out);
}
static char *template_detail(const authoring_navigation *index, const script_catalog_template *definition)
{
    char *signature = template_signature(authoring_navigation_catalog(index), definition);
    const authoring_reference *location = authoring_navigation_definition(
        index,
        (authoring_target){ definition->kind == SCRIPT_TEMPLATE_RECORD ? AUTHORING_TYPE
                                                                       : AUTHORING_CALLABLE,
                            -1,
                            definition->id,
                            0 }
    );
    for (int d = 0; d < authoring_navigation_document_count(index); d++) {
        const authoring_document *document = authoring_navigation_document_at(index, d);
        bool owner = location
                       ? location->location.document == d
                       : document->analysis && definition->owner &&
                             !strcmp(document->analysis->identity, definition->owner);
        if (!owner || definition->start < 0 || definition->end <= definition->start) {
            continue;
        }
        script_syntax *parsed = document->syntax
                                  ? NULL
                                  : script_parse(
                                        authoring_navigation_document_text(index, d)
                                    );
        const script_syntax *syntax = document->syntax ? document->syntax : parsed;
        TSNode root = ts_tree_root_node(syntax->tree);
        TSNode declaration = ts_node_named_descendant_for_byte_range(
            root,
            (uint32_t)definition->start,
            (uint32_t)definition->end
        );
        TSNode comment = ts_node_prev_named_sibling(declaration);
        if (kind(comment, "comment")) {
            char *out = NULL;
            append(&out, signature);
            append(&out, "\n");
            script_range range = script_node_range(comment);
            int size = range.end - range.start;
            if (size <= MAX_DETAIL) {
                char *text = xmalloc((size_t)size + 1);
                memcpy(text, syntax->text + range.start, (size_t)size);
                text[size] = 0;
                append(&out, text);
                free(text);
            }
            script_syntax_free(parsed);
            free(signature);
            return finish_text(out);
        }
        script_syntax_free(parsed);
    }
    return signature;
}
static char *symbol_detail(const authoring_navigation *index, authoring_target target)
{
    const script_catalog *catalog = authoring_navigation_catalog(index);
    const script_catalog_template
        *definition = authoring_navigation_template(index, target.id);
    if (definition) {
        return template_detail(index, definition);
    }
    const script_symbol *s = symbol(index, target);
    if (!s) {
        return NULL;
    }
    if (s->kind != SCRIPT_FUNCTION && s->kind != SCRIPT_MACRO) {
        if (s->kind == SCRIPT_STATE || s->kind == SCRIPT_SHARED_STATE) {
            char *out = NULL;
            append(&out, s->name);
            append(&out, ": retain ");
            type_text(&out, catalog, s->type);
            if (s->unit) {
                append(&out, " ");
                append(&out, s->unit);
            }
            return finish_text(out);
        }
        return declaration(catalog, s->name, s->type, s->unit);
    }
    for (int d = 0; d < authoring_navigation_document_count(index); d++) {
        const script_analysis *a = authoring_navigation_document_at(index, d)->analysis;
        if (!a) {
            continue;
        }
        for (int i = 0; i < a->function_count; i++) {
            const script_function *f = &a->functions[i];
            if (f->symbol < 0 || &a->symbols[f->symbol] != s) {
                continue;
            }
            char *out = NULL;
            append(&out, s->name);
            append(&out, "(");
            for (int p = 0; p < f->parameter_count; p++) {
                const script_symbol *param = &a->symbols[f->parameters[p]];
                if (p) {
                    append(&out, ", ");
                }
                append(&out, param->name);
                append(&out, ": ");
                type_text(&out, catalog, param->type);
                if (param->unit) {
                    append(&out, " ");
                    append(&out, param->unit);
                }
            }
            append(&out, ")");
            if (f->result_count) {
                append(&out, " -> (");
                for (int r = 0; r < f->result_count; r++) {
                    const script_symbol *result = &a->symbols[f->results[r]];
                    if (r) {
                        append(&out, ", ");
                    }
                    append(&out, result->name);
                    append(&out, ": ");
                    type_text(&out, catalog, result->type);
                    if (result->unit) {
                        append(&out, " ");
                        append(&out, result->unit);
                    }
                }
                append(&out, ")");
            }
            return finish_text(out);
        }
    }
    return declaration(catalog, s->name, s->type, s->unit);
}
static char *node_text(const authoring_document *doc, TSNode node)
{
    script_range range = script_node_range(node);
    char *text = xmalloc((size_t)(range.end - range.start) + 1);
    memcpy(text, doc->syntax->text + range.start, (size_t)(range.end - range.start));
    text[range.end - range.start] = 0;
    return text;
}
static char *syntax_hover(const authoring_document *doc, int offset)
{
    if (!doc->syntax || offset < 0 || offset >= (int)strlen(doc->syntax->text)) {
        return NULL;
    }
    TSNode node = ts_node_descendant_for_byte_range(
        ts_tree_root_node(doc->syntax->tree),
        (uint32_t)offset,
        (uint32_t)offset
    );
    for (TSNode p = node; !ts_node_is_null(p); p = ts_node_parent(p)) {
        const char *suffix = kind(p, "type_binder")
                               ? ": Type (inferred from argument)"
                           : kind(p, "prototype_declaration") ? " :: prototype"
                           : kind(p, "relationship_declaration")
                               ? " :: relationship"
                           : kind(p, "record_declaration") ? " :: struct"
                                                           : NULL;
        if (!suffix) {
            continue;
        }
        TSNode name = field(p, "name");
        if (offset < (int)ts_node_start_byte(name) ||
            offset >= (int)ts_node_end_byte(name)) {
            continue;
        }
        char *spelling = node_text(doc, name), *out = NULL;
        append(&out, spelling);
        append(&out, suffix);
        free(spelling);
        return finish_text(out);
    }
    return NULL;
}
char *authoring_source_hover(const authoring_navigation *index, int document, int offset)
{
    const authoring_reference *ref = authoring_navigation_at(index, document, offset);
    if (!ref) {
        return syntax_hover(authoring_navigation_document_at(index, document), offset);
    }
    const script_catalog *catalog = authoring_navigation_catalog(index);
    const script_catalog_template
        *definition = authoring_navigation_template(index, ref->target.id);
    if (definition) {
        return template_detail(index, definition);
    }
    if (ref->target.kind == AUTHORING_PROTOTYPE) {
        const script_catalog_capability
            *cap = script_catalog_capability_by_id(catalog, ref->target.id);
        if (cap && cap->kind == SCRIPT_CAPABILITY_PROTOTYPE) {
            char *out = NULL;
            append(&out, cap->name);
            append(&out, " :: prototype");
            return finish_text(out);
        }
    }
    if (ref->target.kind == AUTHORING_CALLABLE) {
        const script_catalog_capability
            *cap = script_catalog_capability_by_id(catalog, ref->target.id);
        if (cap) {
            return signature(catalog, cap);
        }
    }
    char *local = symbol_detail(index, ref->target);
    if (local) {
        return local;
    }
    const char *name = authoring_navigation_name(index, ref->target);
    return name ? declaration(catalog, name, ref->type, NULL) : NULL;
}
/* Exact, prefix, then fuzzy subsequence matches. Stable ties retain lexical
 * scope priority from the resolved declaration walk. */
static int match_score(const char *name, const char *text, int length)
{
    int i = 0;
    const char *p = name;
    for (; *p && i < length; p++) {
        if (*p == '_' || *p == ' ') {
            continue;
        }
        while (i < length && (text[i] == '_' || text[i] == ' ')) {
            i++;
        }
        if (i < length &&
            tolower((unsigned char)*p) != tolower((unsigned char)text[i])) {
            break;
        }
        if (i < length) {
            i++;
        } else {
            break;
        }
    }
    while (i < length && (text[i] == '_' || text[i] == ' ')) {
        i++;
    }
    if (i == length) {
        while (*p == '_' || *p == ' ') {
            p++;
        }
        return *p ? 1 : 0;
    }
    i = 0;
    for (p = name; *p && i < length; p++) {
        while (i < length && (text[i] == '_' || text[i] == ' ')) {
            i++;
        }
        if (i < length &&
            tolower((unsigned char)*p) == tolower((unsigned char)text[i])) {
            i++;
        }
    }
    while (i < length && (text[i] == '_' || text[i] == ' ')) {
        i++;
    }
    return i == length ? 2 : -1;
}
static void rank_completions(authoring_completions *out, const char *word, int length)
{
    if (!length) {
        return;
    }
    int scores[MAX_ITEMS];
    size_t lengths[MAX_ITEMS];
    for (int i = 0; i < out->count; i++) {
        scores[i] = match_score(out->items[i].label, word, length);
        lengths[i] = strlen(out->items[i].label);
    }
    for (int i = 1; i < out->count; i++) {
        authoring_completion value = out->items[i];
        int score = scores[i], j = i;
        size_t size = lengths[i];
        while (j > 0) {
            if (scores[j - 1] < score ||
                (scores[j - 1] == score && lengths[j - 1] <= size)) {
                break;
            }
            out->items[j] = out->items[j - 1];
            scores[j] = scores[j - 1];
            lengths[j] = lengths[j - 1];
            j--;
        }
        out->items[j] = value;
        scores[j] = score;
        lengths[j] = size;
    }
}
static void offer(
    authoring_completions *out,
    const char *word,
    int length,
    const char *label,
    const char *detail,
    const char *insert,
    authoring_completion_kind kind,
    authoring_target target,
    script_type type
)
{
    if (out->count == MAX_ITEMS) {
        return;
    }
    char *spelling = kind == AUTHORING_COMPLETE_TYPE && strchr(insert, '(')
                       ? xstrdup(insert)
                       : authoring_source_identifier(insert, NULL, 0);
    if (!spelling || match_score(spelling, word, length) < 0) {
        free(spelling);
        return;
    }
    for (int i = 0; i < out->count; i++) {
        if (!strcmp(out->items[i].label, spelling)) {
            free(spelling);
            return;
        }
    }
    (void)label;
    apush(
        out->items,
        ((authoring_completion){ target, xstrdup(spelling), xstrdup(detail), spelling, kind, type })
    );
    out->count = acount(out->items);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static const script_expression *expression(const script_analysis *a, int start, int end)
{
    const script_expression *found = NULL;
    for (int i = 0; i < a->expression_count; i++) {
        const script_expression *e = &a->expressions[i];
        if (e->range.end == end && (start < 0 || e->range.start == start) &&
            !(e->type.flags & SCRIPT_TYPE_ERROR) &&
            (!found || e->range.start < found->range.start)) {
            found = e;
        }
    }
    return found;
}
static script_type node_type(const script_analysis *a, TSNode node)
{
    if (ts_node_is_null(node)) {
        return (script_type){ .flags = SCRIPT_TYPE_ERROR };
    }
    const script_expression
        *e = expression(a, (int)ts_node_start_byte(node), (int)ts_node_end_byte(node));
    return e ? e->type : (script_type){ .flags = SCRIPT_TYPE_ERROR };
}
static int scope_at(const script_analysis *a, int offset)
{
    int scope = 0;
    for (int i = 0; i < a->scope_count; i++) {
        script_range r = a->scopes[i].range;
        if (r.start <= offset && offset <= r.end &&
            r.end - r.start <=
                a->scopes[scope].range.end - a->scopes[scope].range.start) {
            scope = i;
        }
    }
    return scope;
}
static const script_symbol *visible_symbol(const script_analysis *a, int offset, const char *name)
{
    if (!a->scope_count) {
        return NULL;
    }
    for (int scope = scope_at(a, offset); scope >= 0; scope = a->scopes[scope].parent) {
        for (int i = 0; i < a->symbol_count; i++) {
            const script_symbol *s = &a->symbols[i];
            if (s->scope == scope && !strcmp(s->name, name) &&
                !(s->kind == SCRIPT_LOCAL && s->declaration.start >= offset)) {
                return s;
            }
        }
    }
    return NULL;
}
static bool compatible(const script_catalog *catalog, script_type expected, script_type actual)
{
    return (expected.flags & SCRIPT_TYPE_ERROR) ||
           script_catalog_type_assignable(catalog, expected, actual);
}
/* CALL identity survives invalid argument values; value-type lookups must
 * remain strict, while signature context deliberately accepts those records. */
static const script_catalog_parameter *argument_parameter(
    const script_analysis *a,
    const script_catalog *catalog,
    TSNode node,
    int offset
)
{
    for (TSNode p = node; !ts_node_is_null(p); p = ts_node_parent(p)) {
        if (!kind(p, "argument_list")) {
            continue;
        }
        TSNode call = ts_node_parent(p);
        const script_expression *resolved = NULL;
        const script_catalog_capability *cap = NULL;
        for (int i = 0; i < a->expression_count; i++) {
            const script_expression *e = &a->expressions[i];
            if (e->kind == SCRIPT_EXPR_CALL &&
                e->range.start == (int)ts_node_start_byte(call) &&
                e->range.end == (int)ts_node_end_byte(call)) {
                resolved = e;
                cap = script_catalog_capability_by_id(catalog, e->capability);
                break;
            }
        }
        if (!cap) {
            continue;
        }
        for (uint32_t i = 0; i < ts_node_named_child_count(p); i++) {
            TSNode arg = ts_node_named_child(p, i);
            if (kind(arg, "comment") || (int)ts_node_start_byte(arg) > offset ||
                (int)ts_node_end_byte(arg) < offset) {
                continue;
            }
            for (int j = 0; j < resolved->argument_count; j++) {
                const script_argument *value = &resolved->arguments[j];
                if (value->range.start == (int)ts_node_start_byte(arg) &&
                    value->range.end == (int)ts_node_end_byte(arg)) {
                    return value->parameter >= 0 && value->parameter < cap->parameter_count
                             ? &cap->parameters[value->parameter]
                             : NULL;
                }
            }
            /* Never infer a named parameter from its raw spelling. Quoted
             * labels and dotted fields belong to canonical call resolution. */
            return NULL;
        }
        /* Empty/trailing argument context has no argument record yet. Select
         * the first unprovided declared parameter, including receiver binding. */
        for (int i = 0; i < cap->parameter_count; i++) {
            bool provided = false;
            for (int j = 0; j < resolved->argument_count; j++) {
                provided |= resolved->arguments[j].parameter == i;
            }
            if (!provided) {
                return &cap->parameters[i];
            }
        }
    }
    const script_expression *partial = NULL;
    for (int i = 0; i < a->expression_count; i++) {
        const script_expression *e = &a->expressions[i];
        if (e->kind == SCRIPT_EXPR_CALL && e->partial &&
            e->range.start <= offset && e->range.end >= offset &&
            (!partial || e->range.end - e->range.start <
                             partial->range.end - partial->range.start)) {
            partial = e;
        }
    }
    const script_catalog_capability
        *cap = partial ? script_catalog_capability_by_id(catalog, partial->capability)
                       : NULL;
    if (!cap) {
        return NULL;
    }
    for (int i = 0; i < partial->argument_count; i++) {
        const script_argument *arg = &partial->arguments[i];
        if (arg->range.start <= offset && arg->range.end >= offset) {
            return arg->parameter >= 0 && arg->parameter < cap->parameter_count
                     ? &cap->parameters[arg->parameter]
                     : NULL;
        }
    }
    for (int i = 0; i < cap->parameter_count; i++) {
        bool provided = false;
        for (int j = 0; j < partial->argument_count; j++) {
            provided |= partial->arguments[j].parameter == i;
        }
        if (!provided) {
            return &cap->parameters[i];
        }
    }
    return NULL;
}
static script_type expected_type(
    const script_analysis *a,
    const script_catalog *catalog,
    TSNode node,
    int offset
)
{
    for (TSNode p = node; !ts_node_is_null(p); p = ts_node_parent(p)) {
        if (kind(p, "assignment") || kind(p, "bind_declaration")) {
            TSNode value = field(p, "value");
            if (!ts_node_is_null(value) && (int)ts_node_start_byte(value) <= offset) {
                return node_type(a, field(p, "target"));
            }
        }
        if (kind(p, "local_declaration") || kind(p, "state_declaration")) {
            TSNode name = field(p, "name"), value = field(p, "value");
            if (!ts_node_is_null(name) && !ts_node_is_null(value) &&
                (int)ts_node_start_byte(value) <= offset) {
                for (int i = 0; i < a->symbol_count; i++) {
                    if (a->symbols[i].declaration.start ==
                        (int)ts_node_start_byte(name)) {
                        return a->symbols[i].type;
                    }
                }
            }
        }
        if (kind(p, "argument_list")) {
            const script_catalog_parameter *parameter = argument_parameter(a, catalog, p, offset);
            if (parameter) {
                return parameter->type;
            }
        }
    }
    return (script_type){ .flags = SCRIPT_TYPE_ERROR };
}
static script_type recover_receiver(
    const authoring_document *doc,
    const script_catalog *catalog,
    int dot,
    int end
)
{
    /* Delete only the unfinished member. Canonical incremental parse and name
     * resolution recover the receiver; no separate expression grammar exists. */
    script_syntax *syntax = script_edit(doc->syntax, dot, end, "");
    script_analysis
        *analysis = script_analyze_document(syntax, catalog, doc->analysis->identity);
    const script_expression *e = expression(analysis, -1, dot);
    script_type type = e ? e->type : (script_type){ .flags = SCRIPT_TYPE_ERROR };
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return type;
}
static void keywords(
    const authoring_document *doc,
    TSNode previous,
    const char *word,
    int length,
    authoring_completions *out
)
{
    const TSLanguage *language = ts_tree_language(doc->syntax->tree);
    TSLookaheadIterator *it = ts_lookahead_iterator_new(language, ts_node_next_parse_state(previous));
    if (!it) {
        return;
    }
    while (ts_lookahead_iterator_next(it)) {
        TSSymbol symbol = ts_lookahead_iterator_current_symbol(it);
        const char *name = ts_lookahead_iterator_current_symbol_name(it);
        if (ts_language_symbol_type(language, symbol) == TSSymbolTypeAnonymous &&
            isalpha((unsigned char)name[0])) {
            offer(
                out,
                word,
                length,
                name,
                "language keyword",
                name,
                AUTHORING_COMPLETE_KEYWORD,
                (authoring_target){ 0 },
                (script_type){ 0 }
            );
        }
    }
    ts_lookahead_iterator_delete(it);
}
static bool library_members(
    const authoring_navigation *index,
    const authoring_document *doc,
    const script_catalog *catalog,
    int dot,
    int offset,
    const char *word,
    int length,
    authoring_completions *out
)
{
    TSNode root = ts_tree_root_node(doc->syntax->tree);
    TSNode name = ts_node_descendant_for_byte_range(root, (uint32_t)dot - 1, (uint32_t)dot - 1);
    if (!kind(name, "identifier")) {
        return false;
    }
    script_range range = script_node_range(name);
    char *receiver = xmalloc((size_t)(range.end - range.start) + 1);
    memcpy(receiver, doc->syntax->text + range.start, (size_t)(range.end - range.start));
    receiver[range.end - range.start] = 0;
    if (visible_symbol(doc->analysis, offset, receiver)) {
        free(receiver);
        return false;
    }
    const script_analysis *library = NULL;
    for (int i = 0; i < authoring_navigation_document_count(index); i++) {
        const script_analysis *a = authoring_navigation_document_at(index, i)->analysis;
        if (a && a->kind == SCRIPT_PROGRAM_LIBRARY && !strcmp(a->name, receiver)) {
            library = a;
            break;
        }
    }
    bool template_library = false;
    size_t receiver_length = strlen(receiver);
    for (int i = 0; i < catalog->template_count; i++) {
        const script_catalog_template *definition = &catalog->templates[i];
        if (strncmp(definition->name, receiver, receiver_length) ||
            definition->name[receiver_length] != '.') {
            continue;
        }
        template_library = true;
        const char *name = definition->name + receiver_length + 1;
        char *detail = template_detail(index, definition);
        offer(
            out,
            word,
            length,
            name,
            detail,
            name,
            definition->kind == SCRIPT_TEMPLATE_RECORD ? AUTHORING_COMPLETE_TYPE
                                                       : AUTHORING_COMPLETE_CALLABLE,
            (authoring_target){ definition->kind == SCRIPT_TEMPLATE_RECORD
                                    ? AUTHORING_TYPE
                                    : AUTHORING_CALLABLE,
                                -1,
                                definition->id,
                                0 },
            (script_type){ 0 }
        );
        free(detail);
    }
    free(receiver);
    if (!library) {
        rank_completions(out, word, length);
        return template_library;
    }
    for (int i = 0; i < library->symbol_count; i++) {
        const script_symbol *s = &library->symbols[i];
        if (s->declaration.start < 0 ||
            (s->kind != SCRIPT_FUNCTION && s->kind != SCRIPT_MACRO)) {
            continue;
        }
        uint64_t id = s->capability ? s->capability : s->id;
        if (specialization(catalog, id) || authoring_navigation_template(index, id)) {
            continue;
        }
        const script_catalog_capability *cap = script_catalog_capability_by_id(catalog, id);
        if (!cap) {
            continue;
        }
        char *detail = signature(catalog, cap);
        offer(
            out,
            word,
            length,
            s->name,
            detail,
            s->name,
            AUTHORING_COMPLETE_CALLABLE,
            (authoring_target){ AUTHORING_CALLABLE, -1, id, 0 },
            (script_type){ 0 }
        );
        free(detail);
    }
    for (int i = 0; i < catalog->template_count; i++) {
        const script_catalog_template *definition = &catalog->templates[i];
        if (!definition->owner || !library->identity ||
            strcmp(definition->owner, library->identity)) {
            continue;
        }
        const char *name = strrchr(definition->name, '.');
        name = name ? name + 1 : definition->name;
        char *detail = template_detail(index, definition);
        offer(
            out,
            word,
            length,
            name,
            detail,
            name,
            definition->kind == SCRIPT_TEMPLATE_RECORD ? AUTHORING_COMPLETE_TYPE
                                                       : AUTHORING_COMPLETE_CALLABLE,
            (authoring_target){ definition->kind == SCRIPT_TEMPLATE_RECORD
                                    ? AUTHORING_TYPE
                                    : AUTHORING_CALLABLE,
                                -1,
                                definition->id,
                                0 },
            (script_type){ 0 }
        );
        free(detail);
    }
    rank_completions(out, word, length);
    return true;
}

static bool asset_completions(
    const authoring_document *doc,
    const script_catalog *catalog,
    TSNode node,
    const char *word,
    int length,
    authoring_completions *out
)
{
    TSNode component = { 0 };
    bool prototype = false, bases = false, relationship = false;
    for (TSNode p = node; !ts_node_is_null(p); p = ts_node_parent(p)) {
        if (kind(p, "prototype_component")) {
            component = p;
        }
        bases |= kind(p, "prototype_bases");
        prototype |= kind(p, "prototype_declaration");
        relationship |= kind(p, "relationship_declaration");
    }
    if (!prototype && !relationship) {
        return false;
    }
    if (relationship) {
        const char *properties[] = { "exclusive", "acyclic", "traversable", "on_delete_target" };
        for (int i = 0; i < (int)(sizeof properties / sizeof *properties); i++) {
            offer(
                out,
                word,
                length,
                properties[i],
                "relationship property",
                properties[i],
                AUTHORING_COMPLETE_VARIABLE,
                (authoring_target){ 0 },
                (script_type){ 0 }
            );
        }
    } else if (!ts_node_is_null(component)) {
        char *name = node_text(doc, field(component, "name"));
        int found = script_catalog_find_type(catalog, name);
        free(name);
        if (found >= 0) {
            const script_catalog_type *type = &catalog->types[found];
            for (int i = 0; i < type->field_count; i++) {
                const script_catalog_field *f = &type->fields[i];
                char *detail = declaration(catalog, f->name, f->type, f->unit);
                offer(
                    out,
                    word,
                    length,
                    f->name,
                    detail,
                    f->name,
                    AUTHORING_COMPLETE_FIELD,
                    (authoring_target){ AUTHORING_FIELD, -1, f->id, type->id },
                    f->type
                );
                free(detail);
            }
        }
    } else if (bases) {
        for (int i = 0; i < catalog->capability_count; i++) {
            const script_catalog_capability *cap = &catalog->capabilities[i];
            if (cap->kind == SCRIPT_CAPABILITY_PROTOTYPE) {
                offer(
                    out,
                    word,
                    length,
                    cap->name,
                    "prototype",
                    cap->name,
                    AUTHORING_COMPLETE_VARIABLE,
                    (authoring_target){ AUTHORING_PROTOTYPE, -1, cap->id, 0 },
                    (script_type){ 0 }
                );
            }
        }
    } else {
        for (int i = 0; i < catalog->type_count; i++) {
            const script_catalog_type *type = &catalog->types[i];
            if (!type->component || type->internal) {
                continue;
            }
            const char *name = type->source_name ? type->source_name : type->name;
            offer(
                out,
                word,
                length,
                name,
                "component defaults",
                name,
                AUTHORING_COMPLETE_TYPE,
                (authoring_target){ AUTHORING_TYPE, -1, type->id, 0 },
                type->type
            );
        }
    }
    rank_completions(out, word, length);
    return true;
}

void authoring_source_complete(
    const authoring_navigation *index,
    int document,
    int offset,
    authoring_completions *out
)
{
    assert(index && out);
    *out = (authoring_completions){ 0 };
    const authoring_document *doc = authoring_navigation_document_at(index, document);
    const script_analysis *a = doc->analysis;
    if (!doc->syntax) {
        return;
    }
    const script_catalog *catalog = authoring_navigation_catalog(index);
    int size = (int)strlen(doc->syntax->text);
    if (offset < 0 || offset > size) {
        return;
    }
    TSNode root = ts_tree_root_node(doc->syntax->tree);
    int point = offset ? offset - 1 : 0;
    TSNode node = ts_node_descendant_for_byte_range(root, (uint32_t)point, (uint32_t)point);
    out->range = (script_range){ .start = offset, .end = offset, .line = 1, .column = 1 };
    for (int i = 0; i < offset; i++) {
        if (doc->syntax->text[i] == '\n') {
            out->range.line++;
            out->range.column = 1;
        } else {
            out->range.column++;
        }
    }
    if (kind(node, "identifier") && (int)ts_node_start_byte(node) <= offset &&
        offset <= (int)ts_node_end_byte(node)) {
        out->range = script_node_range(node);
    }
    const char *word = doc->syntax->text + out->range.start;
    int length = offset - out->range.start;
    int prior = out->range.start;
    while (prior > 0 && isspace((unsigned char)doc->syntax->text[prior - 1])) {
        prior--;
    }
    TSNode previous = ts_node_descendant_for_byte_range(
        root,
        prior ? (uint32_t)prior - 1 : 0,
        prior ? (uint32_t)prior - 1 : 0
    );
    if (kind(previous, "comment") || kind(node, "comment") ||
        kind(node, "string") || kind(node, "character")) {
        return;
    }
    if (asset_completions(doc, catalog, node, word, length, out) || !a) {
        return;
    }
    bool member = prior > 0 && doc->syntax->text[prior - 1] == '.';
    bool types = false, events = kind(previous, "on") || kind(previous, "after");
    for (TSNode p = node; !ts_node_is_null(p); p = ts_node_parent(p)) {
        types |= kind(p, "type") || kind(p, "type_binder") ||
                 kind(p, "type_application");
        if (kind(p, "handler")) {
            TSNode name = field(p, "name");
            events |= !ts_node_is_null(name) && offset <= (int)ts_node_end_byte(name);
        }
    }
    script_type expected = expected_type(a, catalog, node, offset);
    const script_catalog_parameter *parameter = argument_parameter(a, catalog, node, offset);
    if (parameter && parameter->reference_kind == SCRIPT_REFERENCE_PROTOTYPE) {
        for (int i = 0; i < catalog->capability_count; i++) {
            const script_catalog_capability *cap = &catalog->capabilities[i];
            if (cap->kind != SCRIPT_CAPABILITY_PROTOTYPE) {
                continue;
            }
            char *detail = NULL;
            append(&detail, cap->name);
            append(&detail, " :: prototype");
            detail = finish_text(detail);
            offer(
                out,
                word,
                length,
                cap->name,
                detail,
                cap->name,
                AUTHORING_COMPLETE_VARIABLE,
                (authoring_target){ AUTHORING_PROTOTYPE, -1, cap->id, 0 },
                parameter->type
            );
            free(detail);
        }
        rank_completions(out, word, length);
        return;
    }
    if (member) {
        int dot = prior - 1;
        if (dot > 0 &&
            library_members(index, doc, catalog, dot, offset, word, length, out)) {
            return;
        }
        const script_expression *e = expression(a, -1, dot);
        script_type
            receiver = e ? e->type
                         : recover_receiver(doc, catalog, dot, out->range.end);
        if (receiver.flags & SCRIPT_TYPE_ERROR) {
            return;
        }
        const script_catalog_type
            *type = script_catalog_type_by_id(catalog, receiver.nominal);
        if (receiver.storage == PIN_HANDLE && receiver.nominal == catalog->entity_type) {
            for (int i = 0; i < catalog->type_count; i++) {
                const script_catalog_type *t = &catalog->types[i];
                if (t->internal || specialization(catalog, t->id) || !t->component) {
                    continue;
                }
                const char *name = t->source_name ? t->source_name : t->name;
                offer(
                    out,
                    word,
                    length,
                    name,
                    t->name,
                    name,
                    AUTHORING_COMPLETE_TYPE,
                    (authoring_target){ AUTHORING_TYPE, -1, t->id, 0 },
                    t->type
                );
            }
            return;
        }
        if (type && !events) {
            for (int i = 0; i < type->field_count; i++) {
                const script_catalog_field *f = &type->fields[i];
                if (!compatible(catalog, expected, f->type)) {
                    continue;
                }
                char *detail = declaration(catalog, f->name, f->type, f->unit);
                offer(
                    out,
                    word,
                    length,
                    f->name,
                    detail,
                    f->name,
                    AUTHORING_COMPLETE_FIELD,
                    (authoring_target){ AUTHORING_FIELD, -1, f->id, type->id },
                    f->type
                );
                free(detail);
            }
        }
        for (int i = 0; i < catalog->capability_count; i++) {
            const script_catalog_capability *c = &catalog->capabilities[i];
            if (specialization(catalog, c->id) ||
                authoring_navigation_template(index, c->id)) {
                continue;
            }
            if (!receiver.nominal || !c->method_name ||
                script_catalog_canonical_type(catalog, (script_type){ .nominal = c->receiver })
                        .nominal !=
                    script_catalog_canonical_type(catalog, receiver).nominal ||
                (events && c->kind != SCRIPT_CAPABILITY_EVENT)) {
                continue;
            }
            const char *name = c->method_name ? c->method_name : c->name;
            char *detail = signature(catalog, c);
            offer(
                out,
                word,
                length,
                name,
                detail,
                name,
                c->kind == SCRIPT_CAPABILITY_EVENT ? AUTHORING_COMPLETE_EVENT
                                                   : AUTHORING_COMPLETE_CALLABLE,
                (authoring_target){ AUTHORING_CALLABLE, -1, c->id, 0 },
                (script_type){ 0 }
            );
            free(detail);
        }
        rank_completions(out, word, length);
        return;
    }
    if (!types && !events && a->scope_count) {
        for (int scope = scope_at(a, offset); scope >= 0;
             scope = a->scopes[scope].parent) {
            for (int i = 0; i < a->symbol_count; i++) {
                const script_symbol *s = &a->symbols[i];
                if (specialization(catalog, s->capability ? s->capability : s->id)) {
                    continue;
                }
                if (s->scope != scope || visible_symbol(a, offset, s->name) != s) {
                    continue;
                }
                if (s->kind == SCRIPT_LOCAL && s->declaration.start >= offset) {
                    continue;
                }
                if (!compatible(catalog, expected, s->type)) {
                    continue;
                }
                bool callable = s->kind == SCRIPT_FUNCTION || s->kind == SCRIPT_MACRO;
                authoring_target target = {
                    callable ? AUTHORING_CALLABLE : AUTHORING_SYMBOL,
                    callable ? -1 : document,
                    callable && s->capability ? s->capability : s->id,
                    0
                };
                char *detail = symbol_detail(index, target);
                assert(detail);
                offer(
                    out,
                    word,
                    length,
                    s->name,
                    detail,
                    s->name,
                    callable ? AUTHORING_COMPLETE_CALLABLE : AUTHORING_COMPLETE_VARIABLE,
                    (authoring_target){
                        callable ? AUTHORING_CALLABLE : AUTHORING_SYMBOL,
                        callable ? -1 : document,
                        callable && s->capability ? s->capability : s->id,
                        0 },
                    s->type
                );
                free(detail);
            }
        }
    }
    if (types) {
        const char *names[] = { "Type",      "number",    "char",
                                "Prototype", "Component", "Relationship" };
        for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
            offer(
                out,
                word,
                length,
                names[i],
                "language type",
                names[i],
                AUTHORING_COMPLETE_TYPE,
                (authoring_target){ 0 },
                script_builtin_type(names[i])
            );
        }
        for (int i = PIN_BOOL; i <= PIN_ENUM; i++) {
            const char *name = script_scalar_name((pin_type)i);
            script_type type = script_builtin_type(name);
            offer(out, word, length, name, name, name, AUTHORING_COMPLETE_TYPE, (authoring_target){ 0 }, type);
        }
    }
    if (!events) {
        for (int i = 0; i < catalog->type_count; i++) {
            const script_catalog_type *t = &catalog->types[i];
            if (t->internal || specialization(catalog, t->id)) {
                continue;
            }
            const char *name = t->source_name ? t->source_name : t->name;
            offer(
                out,
                word,
                length,
                name,
                t->name,
                name,
                AUTHORING_COMPLETE_TYPE,
                (authoring_target){ AUTHORING_TYPE, -1, t->id, 0 },
                t->type
            );
        }
    }
    if (!types) {
        for (int i = 0; i < catalog->capability_count; i++) {
            const script_catalog_capability *c = &catalog->capabilities[i];
            if (specialization(catalog, c->id) ||
                authoring_navigation_template(index, c->id)) {
                continue;
            }
            if (c->kind == SCRIPT_CAPABILITY_PROTOTYPE ||
                (events && c->kind != SCRIPT_CAPABILITY_EVENT) ||
                (!events && c->kind == SCRIPT_CAPABILITY_EVENT)) {
                continue;
            }
            if (!events && !(expected.flags & SCRIPT_TYPE_ERROR) &&
                (!c->result_count ||
                 !compatible(catalog, expected, c->results[0].type))) {
                continue;
            }
            const char *name = c->qualified_name ? c->qualified_name : c->name;
            const script_symbol *visible = visible_symbol(a, offset, name);
            if (visible && visible->kind != SCRIPT_FUNCTION &&
                visible->kind != SCRIPT_MACRO) {
                continue;
            }
            char *detail = signature(catalog, c);
            offer(
                out,
                word,
                length,
                name,
                detail,
                name,
                c->kind == SCRIPT_CAPABILITY_EVENT ? AUTHORING_COMPLETE_EVENT
                                                   : AUTHORING_COMPLETE_CALLABLE,
                (authoring_target){ AUTHORING_CALLABLE, -1, c->id, 0 },
                (script_type){ 0 }
            );
            free(detail);
        }
    }
    if (!events) {
        for (int i = 0; i < catalog->template_count; i++) {
            const script_catalog_template *definition = &catalog->templates[i];
            if (types && definition->kind != SCRIPT_TEMPLATE_RECORD &&
                definition->kind != SCRIPT_TEMPLATE_TYPE_FUNCTION) {
                continue;
            }
            char *detail = template_detail(index, definition);
            offer(
                out,
                word,
                length,
                definition->name,
                detail,
                definition->name,
                definition->kind == SCRIPT_TEMPLATE_RECORD ? AUTHORING_COMPLETE_TYPE
                                                           : AUTHORING_COMPLETE_CALLABLE,
                (authoring_target){ definition->kind == SCRIPT_TEMPLATE_RECORD
                                        ? AUTHORING_TYPE
                                        : AUTHORING_CALLABLE,
                                    -1,
                                    definition->id,
                                    0 },
                (script_type){ 0 }
            );
            free(detail);
        }
    }
    keywords(doc, previous, word, length, out);
    rank_completions(out, word, length);
}
