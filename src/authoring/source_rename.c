#include "authoring/source_rename.h"

#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RENAME_MAX_BYTES (64 * 1024 * 1024)
#define RENAME_MAX_REFERENCES 262144
#define RENAME_MAX_NAME 4096
#define RENAME_MAX_DOCUMENTS 1024

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}

void authoring_rename_plan_free(authoring_rename_plan *plan)
{
    if (!plan) {
        return;
    }
    for (int i = 0; i < plan->count; i++) {
        free(plan->edits[i].path);
        free(plan->edits[i].expected);
        free(plan->edits[i].replacement);
    }
    afree(plan->edits);
    free(plan);
}

static const script_symbol *declaration(const authoring_document *document, script_range range)
{
    const script_analysis *a = document->analysis;
    if (!a) {
        return NULL;
    }
    for (int i = 0; i < a->symbol_count; i++) {
        const script_symbol *s = &a->symbols[i];
        if (s->declaration.start == range.start && s->declaration.end == range.end) {
            return s;
        }
    }
    return NULL;
}

static bool ancestor(const script_analysis *a, int outer, int inner)
{
    for (int depth = 0; inner >= 0 && depth <= a->scope_count; depth++) {
        if (inner == outer) {
            return true;
        }
        if (inner >= a->scope_count) {
            return false;
        }
        inner = a->scopes[inner].parent;
    }
    return false;
}

static bool in_scope(const script_analysis *a, int scope, script_range range)
{
    if (scope < 0 || scope >= a->scope_count) {
        return false;
    }
    return a->scopes[scope].range.start <= range.start &&
           a->scopes[scope].range.end >= range.end;
}

/* Named argument labels, qualified members and explicit engine names do not
 * participate in lexical binding capture. Their declaration namespace is
 * checked separately. */
static bool lexical(const authoring_document *document, script_range range)
{
    if (!document->syntax) {
        return false;
    }
    TSNode node = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(document->syntax->tree),
        (uint32_t)range.start,
        (uint32_t)range.end
    );
    if (strcmp(ts_node_type(node), "identifier")) {
        return false;
    }
    TSNode parent = ts_node_parent(node);
    const char *kind = ts_node_type(parent);
    return strcmp(kind, "argument_name") && strcmp(kind, "engine_reference") &&
           (strcmp(kind, "member_expression") ||
            !ts_node_eq(node, ts_node_child_by_field_name(parent, "member", 6)));
}

static bool local_capture(
    const authoring_navigation *index,
    authoring_target target,
    const authoring_reference *definition,
    const script_symbol *selected,
    const char *name
)
{
    const script_analysis
        *a = authoring_navigation_document_at(index, definition->location.document)
                 ->analysis;
    for (int i = 0; i < a->symbol_count; i++) {
        const script_symbol *other = &a->symbols[i];
        if (other == selected || strcmp(other->name, name)) {
            continue;
        }
        if (other->scope == selected->scope) {
            return true;
        }
        if (!ancestor(a, other->scope, selected->scope) &&
            !ancestor(a, selected->scope, other->scope)) {
            continue;
        }
        for (int j = 0; j < authoring_navigation_reference_count(index); j++) {
            const authoring_reference *r = authoring_navigation_reference_at(index, j);
            if (r->location.document != definition->location.document ||
                r->kind == AUTHORING_DEFINE ||
                !lexical(
                    authoring_navigation_document_at(index, r->location.document),
                    r->location.range
                )) {
                continue;
            }
            if (authoring_target_equal(r->target, target) &&
                ancestor(a, selected->scope, other->scope) &&
                in_scope(a, other->scope, r->location.range)) {
                return true;
            }
            if (r->target.kind == AUTHORING_SYMBOL && r->target.id == other->id &&
                r->target.document == definition->location.document &&
                ancestor(a, other->scope, selected->scope) &&
                in_scope(a, selected->scope, r->location.range)) {
                return true;
            }
        }
    }
    return false;
}

static bool same_namespace(const char *a, const char *b)
{
    const char *x = strrchr(a, '.'), *y = strrchr(b, '.');
    if (!x || !y) {
        return !x && !y;
    }
    return x - a == y - b && !strncmp(a, b, (size_t)(x - a));
}

static const char *last_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot ? dot + 1 : name;
}

static bool lexical_capture(const authoring_navigation *index, authoring_target target, const char *name)
{
    /* A call token may otherwise start resolving a local value or action. */
    for (int i = 0; i < authoring_navigation_reference_count(index); i++) {
        const authoring_reference *r = authoring_navigation_reference_at(index, i);
        if (!authoring_target_equal(r->target, target) || r->kind == AUTHORING_DEFINE ||
            !lexical(
                authoring_navigation_document_at(index, r->location.document),
                r->location.range
            )) {
            continue;
        }
        const script_analysis
            *caller = authoring_navigation_document_at(index, r->location.document)
                          ->analysis;
        for (int j = 0; j < caller->symbol_count; j++) {
            const script_symbol *s = &caller->symbols[j];
            if (s->kind != SCRIPT_FUNCTION && s->kind != SCRIPT_MACRO &&
                !strcmp(s->name, name) &&
                in_scope(caller, s->scope, r->location.range)) {
                return true;
            }
        }
    }
    return false;
}

static bool collision(
    const authoring_navigation *index,
    authoring_target target,
    const authoring_reference *definition,
    const script_symbol *selected,
    const char *name
)
{
    const script_catalog *catalog = authoring_navigation_catalog(index);
    if (target.kind == AUTHORING_FIELD) {
        const script_catalog_type
            *owner = script_catalog_type_by_id(catalog, target.owner);
        if (!owner) {
            return true;
        }
        for (int i = 0; i < owner->field_count; i++) {
            const script_catalog_field *field = &owner->fields[i];
            if (field->id != target.id && script_name_matches(field->name, name)) {
                return true;
            }
        }
        return false;
    }
    if (target.kind == AUTHORING_PROGRAM) {
        for (int d = 0; d < authoring_navigation_document_count(index); d++) {
            const script_analysis *a = authoring_navigation_document_at(index, d)
                                           ->analysis;
            if (a && d != definition->location.document && a->name &&
                script_name_matches(a->name, name)) {
                return true;
            }
        }
        for (int i = 0; i < catalog->type_count; i++) {
            if (script_name_matches(catalog->types[i].name, name)) {
                return true;
            }
        }
        for (int i = 0; i < catalog->capability_count; i++) {
            const char *full = catalog->capabilities[i].qualified_name;
            const char *dot = full ? strrchr(full, '.') : NULL;
            if (dot && (size_t)(dot - full) == strlen(name) &&
                !strncmp(full, name, (size_t)(dot - full))) {
                return true;
            }
        }
        return lexical_capture(index, target, name);
    }
    const bool
        external = !authoring_navigation_document_at(index, definition->location.document)
                        ->analysis;
    if (!selected && !external) {
        return true;
    }
    if (target.kind == AUTHORING_SYMBOL) {
        if (local_capture(index, target, definition, selected, name)) {
            return true;
        }
        if (selected->kind == SCRIPT_PARAMETER || selected->kind == SCRIPT_RESULT) {
            const script_analysis *
                a = authoring_navigation_document_at(index, definition->location.document)
                        ->analysis;
            for (int i = 0; i < a->symbol_count; i++) {
                const script_symbol *s = &a->symbols[i];
                if (s != selected && s->scope == selected->scope &&
                    script_name_matches(s->name, name)) {
                    return true;
                }
            }
        }
        return false;
    }
    const script_catalog_capability
        *cap = script_catalog_capability_by_id(catalog, target.id);
    const char *full = cap && cap->qualified_name ? cap->qualified_name
                     : cap                        ? cap->name
                     : selected ? selected->name
                                : authoring_navigation_name(index, target);
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *other = &catalog->capabilities[i];
        const char *qualified = other->qualified_name ? other->qualified_name
                                                      : other->name;
        if (other->id != target.id && same_namespace(full, qualified) &&
            script_name_matches(last_name(qualified), name)) {
            return true;
        }
    }
    const script_analysis
        *a = authoring_navigation_document_at(index, definition->location.document)
                 ->analysis;
    for (int i = 0; a && i < a->symbol_count; i++) {
        const script_symbol *s = &a->symbols[i];
        if (s != selected && selected && s->scope == selected->scope &&
            script_name_matches(s->name, name)) {
            return true;
        }
    }
    return lexical_capture(index, target, name);
}

static bool identifier(const char *name)
{
    unsigned char c = (unsigned char)*name++;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
        return false;
    }
    while ((c = (unsigned char)*name++)) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static char *quoted(const char *name)
{
    size_t length = strlen(name);
    char *out = xmalloc(length * 2 + 3), *at = out;
    *at++ = '"';
    for (size_t i = 0; i < length; i++) {
        if (name[i] == '"' || name[i] == '\\') {
            *at++ = '\\';
        }
        *at++ = name[i];
    }
    *at++ = '"';
    *at = 0;
    return out;
}

static char *replacement(
    const authoring_document *document,
    source_text_kind kind,
    const authoring_reference *r,
    const char *source,
    const char *name
)
{
    if (kind == SOURCE_TEXT_RML) {
        return identifier(name) ? xstrdup(name) : NULL;
    }
    if (document->syntax) {
        return identifier(name) ? xstrdup(name) : NULL;
    }
    if (source[r->location.range.start] == '"') {
        return quoted(name);
    }
    /* A bare KDL name, like an input map action's node, stays bare. */
    return identifier(name) ? xstrdup(name) : quoted(name);
}

static int compare_edits(const void *left, const void *right)
{
    const authoring_rename_edit *a = left, *b = right;
    if (a->document != b->document) {
        return (a->document > b->document) - (a->document < b->document);
    }
    return (a->range.start > b->range.start) - (a->range.start < b->range.start);
}

char *authoring_rename_document(
    const authoring_rename_plan *plan,
    int document,
    const char *source,
    char *error,
    size_t size
)
{
    size_t length = strlen(source), output = length;
    if (length > RENAME_MAX_BYTES) {
        fail(error, size, "Source exceeds rename byte limit");
        return NULL;
    }
    int offset = 0;
    for (int i = 0; i < plan->count; i++) {
        const authoring_rename_edit *edit = &plan->edits[i];
        if (edit->document != document) {
            continue;
        }
        if (edit->range.start < offset || edit->range.end < edit->range.start ||
            (size_t)edit->range.end > length ||
            strlen(edit->expected) != (size_t)(edit->range.end - edit->range.start) ||
            memcmp(source + edit->range.start, edit->expected, strlen(edit->expected))) {
            fail(error, size, "Source changed since rename was planned");
            return NULL;
        }
        output -= (size_t)(edit->range.end - edit->range.start);
        size_t added = strlen(edit->replacement);
        if (added > RENAME_MAX_BYTES - output) {
            fail(error, size, "Renamed source exceeds byte limit");
            return NULL;
        }
        output += added;
        offset = edit->range.end;
    }
    char *text = xmalloc(output + 1), *at = text;
    offset = 0;
    for (int i = 0; i < plan->count; i++) {
        const authoring_rename_edit *edit = &plan->edits[i];
        if (edit->document != document) {
            continue;
        }
        size_t count = (size_t)(edit->range.start - offset);
        memcpy(at, source + offset, count);
        at += count;
        count = strlen(edit->replacement);
        memcpy(at, edit->replacement, count);
        at += count;
        offset = edit->range.end;
    }
    memcpy(at, source + offset, length - (size_t)offset + 1);
    return text;
}

static bool syntax_valid(const authoring_navigation *index, const authoring_rename_plan *plan)
{
    for (int first = 0; first < plan->count;) {
        int document = plan->edits[first].document;
        const authoring_document *input = authoring_navigation_document_at(index, document);
        const char *source = authoring_navigation_document_text(index, document);
        char *text = authoring_rename_document(plan, document, source, NULL, 0);
        if (!text) {
            return false;
        }
        bool valid;
        if (authoring_navigation_document_kind(index, document) == SOURCE_TEXT_RML) {
            /* Exact event spans receive identifier tokens only. No quote,
             * binding separator or markup delimiter can change. The complete
             * project candidate still requires semantic validation. */
            valid = true;
        } else if (input->syntax || authoring_navigation_document_kind(index, document) == SOURCE_TEXT_CHIBI) {
            script_syntax *syntax = script_parse(text);
            valid = !syntax->diagnostic_count;
            script_syntax_free(syntax);
        } else {
            char error[192];
            av_doc *value = asset_text_read(text, error, sizeof error);
            valid = value != NULL;
            av_doc_free(value);
        }
        free(text);
        if (!valid) {
            return false;
        }
        do {
            first++;
        } while (first < plan->count && plan->edits[first].document == document);
    }
    return true;
}

authoring_rename_plan *authoring_source_rename(
    const authoring_navigation *index,
    authoring_target target,
    const char *name,
    char *error,
    size_t size
)
{
    if (size) {
        error[0] = 0;
    }
    if (!name || !*name || strlen(name) > RENAME_MAX_NAME) {
        fail(error, size, "Invalid or oversized name");
        return NULL;
    }
    for (size_t i = 0; name[i]; i++) {
        if ((unsigned char)name[i] < 32) {
            fail(error, size, "Name contains an unrepresentable control byte");
            return NULL;
        }
    }
    int documents = authoring_navigation_document_count(index);
    if (documents > RENAME_MAX_DOCUMENTS ||
        authoring_navigation_reference_count(index) > RENAME_MAX_REFERENCES) {
        fail(error, size, "Rename index exceeds limits");
        return NULL;
    }
    size_t bytes = 0;
    for (int i = 0; i < documents; i++) {
        const authoring_document *document = authoring_navigation_document_at(index, i);
        if (document->analysis &&
            (!document->analysis->valid || document->syntax->diagnostic_count ||
             strcmp(document->analysis->text, document->syntax->text))) {
            fail(error, size, "Rename requires a complete valid project index");
            return NULL;
        }
        const char *source = authoring_navigation_document_text(index, i);
        if (!source) {
            fail(error, size, "Missing rename document text");
            return NULL;
        }
        size_t n = strlen(source);
        if (n > RENAME_MAX_BYTES - bytes) {
            fail(error, size, "Rename source exceeds byte limit");
            return NULL;
        }
        bytes += n;
    }
    const authoring_reference *definition = authoring_navigation_definition(index, target);
    if (!definition || target.kind == AUTHORING_TYPE) {
        fail(error, size, "Target has no editable source declaration");
        return NULL;
    }
    const authoring_document
        *document = authoring_navigation_document_at(index, definition->location.document);
    const script_symbol *selected = declaration(document, definition->location.range);
    const char *old_name = authoring_navigation_name(index, target);
    if (old_name && !strcmp(old_name, name)) {
        return xcalloc(1, sizeof(authoring_rename_plan));
    }
    if (collision(index, target, definition, selected, name)) {
        fail(error, size, "Rename would collide with or capture another declaration");
        return NULL;
    }
    authoring_rename_plan *plan = xcalloc(1, sizeof *plan);
    bytes = 0;
    for (int i = 0; i < authoring_navigation_reference_count(index); i++) {
        const authoring_reference *r = authoring_navigation_reference_at(index, i);
        if (!authoring_target_equal(r->target, target)) {
            continue;
        }
        const authoring_document
            *d = authoring_navigation_document_at(index, r->location.document);
        const char
            *source = authoring_navigation_document_text(index, r->location.document);
        int start = r->location.range.start, end = r->location.range.end;
        if (start < 0 || end <= start || (size_t)end > strlen(source)) {
            fail(error, size, "Reference range is stale");
            goto failure;
        }
        char *text = replacement(
            d,
            authoring_navigation_document_kind(index, r->location.document),
            r,
            source,
            name
        );
        if (!text) {
            fail(error, size, "Name cannot be represented at every reference");
            goto failure;
        }
        size_t n = sizeof(authoring_rename_edit) + strlen(d->path) +
                   (size_t)(end - start) + strlen(text) + 3;
        if (n > RENAME_MAX_BYTES - bytes) {
            free(text);
            fail(error, size, "Rename edits exceed byte limit");
            goto failure;
        }
        bytes += n;
        char *expected = xmalloc((size_t)(end - start) + 1);
        memcpy(expected, source + start, (size_t)(end - start));
        expected[end - start] = 0;
        apush(
            plan->edits,
            ((
                authoring_rename_edit
            ){ r->location.document, xstrdup(d->path), r->location.range, expected, text })
        );
        plan->count++;
    }
    qsort(plan->edits, (size_t)plan->count, sizeof *plan->edits, compare_edits);
    for (int i = 1; i < plan->count; i++) {
        authoring_rename_edit *a = &plan->edits[i - 1], *b = &plan->edits[i];
        if (a->document != b->document || a->range.end <= b->range.start) {
            continue;
        }
        if (a->range.start == b->range.start && a->range.end == b->range.end) {
            free(b->path);
            free(b->expected);
            free(b->replacement);
            memmove(b, b + 1, (size_t)(plan->count - i - 1) * sizeof *b);
            plan->count--;
            asetlen(plan->edits, plan->count);
            i--;
            continue;
        }
        fail(error, size, "Resolved reference ranges overlap");
        goto failure;
    }
    if (!syntax_valid(index, plan)) {
        fail(error, size, "Name cannot be represented by valid source syntax");
        goto failure;
    }
    return plan;
failure:
    authoring_rename_plan_free(plan);
    return NULL;
}
