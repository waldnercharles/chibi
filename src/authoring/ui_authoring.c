#include "authoring/ui_authoring.h"

#include "assets/ui_source.h"
#include "support/array.h"
#include "support/text_edit.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ui_authoring
{
    workspace_documents *documents;
    ui_source *source;
    int document, revision;
    char error[512];
};
ui_authoring *ui_authoring_new(workspace_documents *documents)
{
    assert(documents);
    ui_authoring *a = xcalloc(1, sizeof *a);
    a->documents = documents;
    return a;
}
void ui_authoring_free(ui_authoring *a)
{
    if (!a) {
        return;
    }
    ui_source_free(a->source);
    free(a);
}
static ui_authoring_status status(ui_authoring *a, int document, int revision, int code, const char *error)
{
    snprintf(a->error, sizeof a->error, "%s", error ? error : "");
    return (ui_authoring_status){ .ok = code == WORKSPACE_DOCUMENT_OK,
                                  .valid = code == WORKSPACE_DOCUMENT_OK,
                                  .code = code,
                                  .document = document,
                                  .revision = revision,
                                  .error = a->error };
}
static ui_authoring_status load(ui_authoring *a, int document, int revision, const char *language)
{
    workspace_document_view view;
    int code = workspace_document_read(a->documents, document, &view);
    if (code) {
        return status(a, document, revision, code, workspace_document_status_text(code));
    }
    if (view.revision != revision) {
        return status(a, document, view.revision, WORKSPACE_DOCUMENT_STALE, "UI source revision changed");
    }
    if (!language || (strcmp(language, "rml") && strcmp(language, "rcss"))) {
        return status(a, document, revision, WORKSPACE_DOCUMENT_INVALID, "Language must be rml or rcss");
    }
    ui_source_language kind = !strcmp(language, "rml") ? UI_SOURCE_RML : UI_SOURCE_RCSS;
    if (!a->source || a->document != document || a->revision != revision ||
        a->source->language != kind) {
        ui_source_free(a->source);
        a->source = ui_source_parse(view.text, kind);
        a->document = document;
        a->revision = revision;
    }
    int entries = acount(a->source->elements) + acount(a->source->rules), details = 0;
    if (entries > 4096) {
        return status(a, document, revision, WORKSPACE_DOCUMENT_LIMIT, "UI source exceeds 4096 element/rule budget");
    }
    int depths[4096];
    for (int i = 0; i < acount(a->source->elements); i++) {
        ui_source_element *e = &a->source->elements[i];
        assert(e->parent < i);
        depths[i] = e->parent < 0 ? 0 : depths[e->parent] + 1;
        if (depths[i] > 64) {
            return status(a, document, revision, WORKSPACE_DOCUMENT_LIMIT, "UI source exceeds depth budget");
        }
        details += acount(e->attributes);
    }
    for (int i = 0; i < acount(a->source->rules); i++) {
        ui_source_rule *r = &a->source->rules[i];
        assert(r->parent < i);
        depths[i] = r->parent < 0 ? 0 : depths[r->parent] + 1;
        if (depths[i] > 64) {
            return status(a, document, revision, WORKSPACE_DOCUMENT_LIMIT, "UI style source exceeds depth budget");
        }
        details += acount(r->declarations);
    }
    if (details > 16384) {
        return status(a, document, revision, WORKSPACE_DOCUMENT_LIMIT, "UI source exceeds attribute/declaration budget");
    }
    ui_authoring_status result = status(a, document, revision, 0, "");
    result.valid = acount(a->source->diagnostics) == 0;
    if (!result.valid) {
        snprintf(a->error, sizeof a->error, "%s", a->source->diagnostics[0].message);
    }
    return result;
}
static const char *copy(arena *a, const char *text, int start, int end)
{
    assert(start >= 0 && end >= start);
    char *out = arena_alloc(a, (size_t)(end - start) + 1);
    memcpy(out, text + start, (size_t)(end - start));
    out[end - start] = 0;
    return out;
}
static const char *string(arena *a, const char *text)
{
    return copy(a, text ? text : "", 0, (int)strlen(text ? text : ""));
}
ui_authoring_status ui_authoring_query(
    ui_authoring *a,
    int document,
    int revision,
    const char *language,
    const char *kind,
    int parent,
    int first,
    int count,
    arena *storage,
    pin_array **rows
)
{
    assert(a && storage && rows);
    *rows = pin_array_snapshot_allocate(storage, PIN_STRUCT, "UiSourceRow", 0);
    ui_authoring_status result = load(a, document, revision, language);
    if (!result.ok) {
        return result;
    }
    ui_source *s = a->source;
    if (!kind || first < 0 || count < 0 || count > 256) {
        return status(a, document, revision, WORKSPACE_DOCUMENT_INVALID, "Expected a bounded source page");
    }
    int total = 0, mode = -1;
    if (!strcmp(kind, "element") && s->language == UI_SOURCE_RML) {
        mode = 0;
        total = acount(s->elements);
    }
    if (!strcmp(kind, "attribute") && s->language == UI_SOURCE_RML &&
        parent >= 0 && parent < acount(s->elements)) {
        mode = 1;
        total = acount(s->elements[parent].attributes);
    }
    if (!strcmp(kind, "rule") && s->language == UI_SOURCE_RCSS) {
        mode = 2;
        total = acount(s->rules);
    }
    if (!strcmp(kind, "declaration") && s->language == UI_SOURCE_RCSS &&
        parent >= 0 && parent < acount(s->rules)) {
        mode = 3;
        total = acount(s->rules[parent].declarations);
    }
    if (!strcmp(kind, "diagnostic")) {
        mode = 4;
        total = acount(s->diagnostics);
    }
    if (mode < 0) {
        return status(a, document, revision, WORKSPACE_DOCUMENT_INVALID, "Source kind/parent does not match language");
    }
    result.total = total;
    int take = first >= total ? 0 : total - first;
    if (take > count) {
        take = count;
    }
    *rows = pin_array_snapshot_allocate(storage, PIN_STRUCT, "UiSourceRow", take);
    for (int j = 0; j < take; j++) {
        int i = first + j;
        ui_authoring_row *r = arena_alloc(storage, sizeof *r);
        *r = (ui_authoring_row){ .index = i,
                                 .parent = parent,
                                 .name = "",
                                 .value = "",
                                 .extra = "" };
        if (mode == 0) {
            ui_source_element *e = &s->elements[i];
            r->parent = e->parent;
            r->name = string(storage, e->tag);
            r->value = string(storage, e->id);
            r->extra = string(storage, e->classes);
            r->start = e->start;
            r->end = e->end;
            r->line = e->line;
            for (int p = e->parent; p >= 0; p = s->elements[p].parent) {
                r->depth++;
            }
        } else if (mode == 1) {
            ui_source_attribute *e = &s->elements[parent].attributes[i];
            r->name = copy(storage, s->text, e->name_start, e->name_end);
            char *decoded = ui_source_attribute_value(s, parent, r->name);
            r->value = string(storage, decoded);
            free(decoded);
            r->binding = ui_source_is_binding(r->name, (int)strlen(r->name));
            r->start = e->start;
            r->end = e->end;
            int column;
            ui_source_position(s, e->start, &r->line, &column);
        } else if (mode == 2) {
            ui_source_rule *e = &s->rules[i];
            r->parent = e->parent;
            r->name = string(storage, e->selector);
            r->start = e->start;
            r->end = e->end;
            r->line = e->line;
            for (int p = e->parent; p >= 0; p = s->rules[p].parent) {
                r->depth++;
            }
        } else if (mode == 3) {
            ui_source_declaration *e = &s->rules[parent].declarations[i];
            r->name = copy(storage, s->text, e->property_start, e->property_end);
            r->value = copy(storage, s->text, e->value_start, e->value_end);
            r->start = e->start;
            r->end = e->end;
            int column;
            ui_source_position(s, e->start, &r->line, &column);
        } else {
            ui_source_diagnostic *e = &s->diagnostics[i];
            r->name = string(storage, e->message);
            r->start = e->start;
            r->end = e->end;
            r->line = e->line;
        }
        (*rows)->items[j] = (pin_value){ .type = PIN_STRUCT,
                                         .type_name = "UiSourceRow",
                                         .ptr = r };
    }
    return result;
}
ui_authoring_status ui_authoring_edit(
    ui_authoring *a,
    int document,
    int revision,
    const char *operation,
    int element,
    int target,
    const char *place,
    const char *name,
    const char *value,
    bool remove
)
{
    bool stylesheet = operation && (!strcmp(operation, "declaration") ||
                                    !strcmp(operation, "selector") ||
                                    !strcmp(operation, "remove-rule"));
    ui_authoring_status result = load(a, document, revision, stylesheet ? "rcss" : "rml");
    if (!result.ok || !result.valid) {
        result.ok = false;
        if (!result.code) {
            result.code = WORKSPACE_DOCUMENT_INVALID;
        }
        return result;
    }
    if (!operation || !name || !value || !place || strlen(name) > 256 ||
        strlen(value) > 65536) {
        return status(a, document, revision, WORKSPACE_DOCUMENT_INVALID, "UI edit argument exceeds bounds");
    }
    ui_source *s = a->source;
    ui_source_place at = UI_SOURCE_INSIDE;
    if (!strcmp(place, "before")) {
        at = UI_SOURCE_BEFORE;
    } else if (!strcmp(place, "after")) {
        at = UI_SOURCE_AFTER;
    } else if (strcmp(place, "inside")) {
        return status(a, document, revision, WORKSPACE_DOCUMENT_INVALID, "Place must be inside, before or after");
    }
    text_edit *edits = NULL;
    bool ok = false;
    char error[512] = "Unknown UI edit operation";
    if (stylesheet && element >= 0 && element < acount(s->rules)) {
        ui_source_rule *rule = &s->rules[element];
        if (!strcmp(operation, "remove-rule")) {
            apush(edits, ((text_edit){ rule->start, rule->end, xstrdup("") }));
            ok = true;
        } else if (!strcmp(operation, "selector") && value[0]) {
            size_t size = strlen(value) + 2;
            char *selector = xmalloc(size);
            snprintf(selector, size, "%s ", value);
            apush(edits, ((text_edit){ rule->start, rule->block_start, selector }));
            ok = true;
        } else if (!strcmp(operation, "declaration") && target >= -1 && target < acount(rule->declarations)) {
            bool property = name[0];
            for (const char *p = name; *p; p++) {
                if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                      (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) {
                    property = false;
                }
            }
            if (remove && target >= 0) {
                ui_source_declaration *declaration = &rule->declarations[target];
                apush(edits, ((text_edit){ declaration->start, declaration->end, xstrdup("") }));
                ok = true;
            } else if (!remove && property) {
                size_t size = strlen(name) + strlen(value) + 8;
                char *declaration = xmalloc(size);
                snprintf(declaration, size, "%s: %s;", name, value);
                int start = target < 0 ? rule->end - 1
                                       : rule->declarations[target].start;
                int end = target < 0 ? start : rule->declarations[target].end;
                apush(edits, ((text_edit){ start, end, declaration }));
                ok = true;
            }
        }
        if (!ok) {
            snprintf(error, sizeof error, "Style edit requires a valid rule, declaration and property");
        }
    } else if (!strcmp(operation, "attribute")) {
        ok = remove
               ? ui_source_remove_attribute(s, element, name, &edits, error, sizeof error)
               : ui_source_set_attribute(s, element, name, value, &edits, error, sizeof error);
    } else if (!strcmp(operation, "class")) {
        ok = remove
               ? ui_source_remove_class(s, element, name, &edits, error, sizeof error)
               : ui_source_add_class(s, element, name, &edits, error, sizeof error);
    } else if (!strcmp(operation, "style")) {
        ok = ui_source_set_style(s, element, name, remove ? NULL : value, &edits, error, sizeof error);
    } else if (!strcmp(operation, "insert")) {
        ok = ui_source_insert_element(s, target, at, value, &edits, error, sizeof error);
    } else if (!strcmp(operation, "move")) {
        ok = ui_source_move_element(s, element, target, at, &edits, error, sizeof error);
    } else if (!strcmp(operation, "delete")) {
        ok = ui_source_delete_element(s, element, &edits, error, sizeof error);
    } else if (!strcmp(operation, "duplicate") && element >= 0 && element < acount(s->elements)) {
        ui_source_element *e = &s->elements[element];
        char *markup = xmalloc((size_t)(e->end - e->start) + 1);
        memcpy(markup, s->text + e->start, (size_t)(e->end - e->start));
        markup[e->end - e->start] = 0;
        ok = ui_source_insert_element(s, element, UI_SOURCE_AFTER, markup, &edits, error, sizeof error);
        free(markup);
    } else if (!strcmp(operation, "text") && element >= 0 && element < acount(s->elements)) {
        ui_source_element *e = &s->elements[element];
        ok = !e->self_closing;
        for (int i = element + 1; i < acount(s->elements); i++) {
            if (s->elements[i].parent == element) {
                ok = false;
            }
        }
        if (ok) {
            char *escaped = xmalloc(strlen(value) * 5 + 1);
            size_t n = 0;
            for (const char *p = value; *p; p++) {
                const char *replacement = *p == '&' ? "&amp;"
                                        : *p == '<' ? "&lt;"
                                        : *p == '>' ? "&gt;"
                                                    : NULL;
                if (replacement) {
                    size_t k = strlen(replacement);
                    memcpy(escaped + n, replacement, k);
                    n += k;
                } else {
                    escaped[n++] = *p;
                }
            }
            escaped[n] = 0;
            apush(edits, ((text_edit){ e->open_end, e->close_start, escaped }));
        } else {
            snprintf(error, sizeof error, "Text editing requires a nonempty container without child elements");
        }
    }
    if (!ok) {
        ui_source_edits_free(edits);
        return status(a, document, revision, WORKSPACE_DOCUMENT_INVALID, error);
    }
    char *text = text_edits_apply(s->text, edits, acount(edits));
    ui_source_edits_free(edits);
    if (!text) {
        return status(a, document, revision, WORKSPACE_DOCUMENT_INVALID, "UI edit ranges overlap");
    }
    ui_source *check = ui_source_parse(text, stylesheet ? UI_SOURCE_RCSS : UI_SOURCE_RML);
    bool syntax = acount(check->diagnostics) == 0;
    bool bounded = acount(check->elements) + acount(check->rules) <= 4096;
    int details = 0;
    for (int i = 0; bounded && i < acount(check->elements); i++) {
        details += acount(check->elements[i].attributes);
        int depth = 0;
        for (int parent = check->elements[i].parent; parent >= 0;
             parent = check->elements[parent].parent) {
            if (++depth > 64) {
                bounded = false;
                break;
            }
        }
    }
    for (int i = 0; bounded && i < acount(check->rules); i++) {
        details += acount(check->rules[i].declarations);
        int depth = 0;
        for (int parent = check->rules[i].parent; parent >= 0;
             parent = check->rules[parent].parent) {
            if (++depth > 64) {
                bounded = false;
                break;
            }
        }
    }
    bounded = bounded && details <= 16384;
    ui_source_free(check);
    if (!bounded) {
        free(text);
        return status(a, document, revision, WORKSPACE_DOCUMENT_LIMIT, "UI edit exceeds structural budget");
    }
    if (!syntax) {
        free(text);
        return status(a, document, revision, WORKSPACE_DOCUMENT_INVALID, "UI edit would create invalid source");
    }
    int code = workspace_document_edit(a->documents, document, revision, 0, (int)strlen(s->text), text);
    free(text);
    if (code) {
        return status(a, document, revision, code, workspace_document_status_text(code));
    }
    workspace_document_view view;
    code = workspace_document_read(a->documents, document, &view);
    assert(!code);
    return status(a, document, view.revision, 0, "");
}
