#include "automation/ui_edit.h"

#include "assets/ui_document.h"
#include "assets/ui_source.h"
#include "automation/controller.h"
#include "render/ui_check.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "ui/rml_backend.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static const char *const BLOCK_NAMES[] = { "rml", "rcss" };

static int failf(char *error, size_t size, int status, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
    return status;
}

/* ---------- Validation ---------- */

typedef struct diagnostic
{
    const char *rule;
    const char *block;  /* NULL outside the blocks */
    const char *source; /* a widget or import the line is in, or NULL */
    int block_line;     /* in `source` when set; 0 when unknown */
    int line, column;   /* in the file; 0 when unknown */
    const char *message;
} diagnostic;

static void add_diagnostic(yyjson_mut_doc *doc, yyjson_mut_val *array, const diagnostic *d)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_str(doc, object, "severity", "error");
    yyjson_mut_obj_add_str(doc, object, "rule_name", d->rule);
    yyjson_mut_obj_add_strcpy(doc, object, "message", d->message);
    if (d->block) {
        yyjson_mut_obj_add_str(doc, object, "block", d->block);
    }
    if (d->source) {
        yyjson_mut_obj_add_strcpy(doc, object, "source", d->source);
    }
    if (d->block_line) {
        yyjson_mut_obj_add_int(doc, object, "block_line", d->block_line);
    }
    if (d->line) {
        yyjson_mut_obj_add_int(doc, object, "line", d->line);
        yyjson_mut_obj_add_int(doc, object, "column", d->column);
    }
}

static const ui_source_block *block_of(const ui_source *source, ui_source_language language)
{
    for (int i = 0; i < acount(source->blocks); i++) {
        if (source->blocks[i].language == language) {
            return &source->blocks[i];
        }
    }
    return NULL;
}

static int syntax_diagnostics(const ui_source *source, yyjson_mut_doc *doc, yyjson_mut_val *diagnostics)
{
    for (int i = 0; i < acount(source->diagnostics); i++) {
        const ui_source_diagnostic *d = &source->diagnostics[i];
        const ui_source_block *block = ui_source_block_at(source, d->start);
        add_diagnostic(
            doc,
            diagnostics,
            &(diagnostic){ .rule = block ? "SYNTAX" : "LOAD",
                           .block = block ? BLOCK_NAMES[block->language] : NULL,
                           .block_line = block ? d->line - block->line + 1 : 0,
                           .line = d->line,
                           .column = d->column,
                           .message = d->message }
        );
    }
    return acount(source->diagnostics);
}

/* A block line's file line, when the block's lines are the file's. */
static int file_line(const ui_source *source, ui_source_language language, int block_line)
{
    const ui_source_block *block = block_of(source, language);
    if (!block || !block_line) {
        return 0;
    }
    int end_line, column;
    ui_source_position(source, block->end, &end_line, &column);
    return end_line > block->line ? block->line + block_line - 1 : block->line;
}

int automation_ui_validate(
    const char *content_root,
    const char *name,
    const char *text,
    yyjson_mut_doc *doc,
    yyjson_mut_val *diagnostics
)
{
    static const char *const PARTS[] = { "rml", "rcss", "widgets" };
    ui_source *source = ui_source_parse(text, false);
    int errors = syntax_diagnostics(source, doc, diagnostics);
    ui_check_message *messages = NULL;
    char error[256];
    if (!errors && !ui_check(content_root, name, text, &messages, error, sizeof error)) {
        add_diagnostic(doc, diagnostics, &(diagnostic){ .rule = "LOAD", .message = error });
        errors++;
    }
    for (int i = 0; i < acount(messages); i++) {
        const ui_check_message *m = &messages[i];
        bool own = !m->source && m->part != UI_CHECK_WIDGETS;
        ui_source_language block = m->part == UI_CHECK_RML ? UI_SOURCE_RML
                                                           : UI_SOURCE_RCSS;
        int line = own ? file_line(source, block, m->line) : 0;
        add_diagnostic(
            doc,
            diagnostics,
            &(diagnostic){ .rule = "RMLUI",
                           .block = PARTS[m->part],
                           .source = m->source,
                           .block_line = m->line,
                           .line = line,
                           .column = line ? 1 : 0,
                           .message = m->text }
        );
    }
    errors += acount(messages);
    ui_check_messages_free(messages);
    ui_source_free(source);
    return errors;
}

/* ---------- Inspection ---------- */

static void add_names(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, char **names)
{
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, object, key);
    for (int i = 0; i < acount(names); i++) {
        yyjson_mut_arr_add_strcpy(doc, array, names[i]);
    }
}

static void free_names(char **names)
{
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
}

/* The source's text in [start, end), owned. */
static char *slice(const ui_source *source, int start, int end)
{
    char *text = xmalloc((size_t)(end - start) + 1);
    memcpy(text, source->text + start, (size_t)(end - start));
    text[end - start] = '\0';
    return text;
}

static bool word_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* Adds the variables an expression reads: each name not after a '.' nor
 * before a '(', outside strings. */
static void expression_roots(const char *text, char ***roots)
{
    static const char *const LITERALS[] = { "true", "false", "null", NULL };
    for (const char *p = text; *p;) {
        if (*p == '\'' || *p == '"') {
            const char *close = strchr(p + 1, *p);
            p = close ? close + 1 : p + strlen(p);
            continue;
        }
        if (!isalpha((unsigned char)*p) && *p != '_') {
            p++;
            continue;
        }
        const char *start = p;
        while (word_char(*p)) {
            p++;
        }
        const char *after = p + strspn(p, " ");
        bool member = start > text && start[-1] == '.';
        char name[128];
        snprintf(name, sizeof name, "%.*s", (int)(p - start), start);
        bool literal = false;
        for (int i = 0; LITERALS[i]; i++) {
            literal |= !strcmp(name, LITERALS[i]);
        }
        if (!member && *after != '(' && !literal) {
            ui_source_list_add(roots, name);
        }
    }
}

/* data-for="item : list", "item, index : list" or "list": the names it
 * defines, and the expression it reads. */
static const char *for_names(const char *value, char ***defined)
{
    const char *colon = strchr(value, ':');
    if (!colon) {
        ui_source_list_add(defined, "it");
        ui_source_list_add(defined, "it_index");
        return value;
    }
    char names[256];
    snprintf(names, sizeof names, "%.*s", (int)(colon - value), value);
    for (char *name = strtok(names, ", "); name; name = strtok(NULL, ", ")) {
        ui_source_list_add(defined, name);
    }
    return colon + 1;
}

/* The props the markup's bindings read: names neither the data model's
 * (save, system), an event's (ev), nor defined by data-for or data-alias-. */
static char **props(const ui_source *source)
{
    char **roots = NULL, **defined = NULL;
    for (int i = 0; i < acount(source->elements); i++) {
        const ui_source_element *e = &source->elements[i];
        for (int a = 0; a < acount(e->attributes); a++) {
            const ui_source_attribute *attribute = &e->attributes[a];
            char *name = slice(source, attribute->name_start, attribute->name_end);
            char *value = ui_source_is_binding(name, (int)strlen(name))
                            ? ui_source_attribute_value(source, i, name)
                            : NULL;
            if (value && !strncmp(name, "data-alias-", 11)) {
                ui_source_list_add(&defined, name + 11);
            }
            if (value) {
                expression_roots(!strcmp(name, "data-for") ? for_names(value, &defined) : value, &roots);
            }
            free(value);
            free(name);
        }
    }
    const ui_source_block *block = block_of(source, UI_SOURCE_RML);
    char *markup = block ? slice(source, block->start, block->end) : xstrdup("");
    for (const char *p = markup; (p = strstr(p, "{{"));) {
        const char *close = strstr(p, "}}");
        if (!close) {
            break;
        }
        char *expression = xmalloc((size_t)(close - p));
        snprintf(expression, (size_t)(close - p), "%s", p + 2);
        expression_roots(expression, &roots);
        free(expression);
        p = close + 2;
    }
    free(markup);
    char **out = NULL;
    for (int i = 0; i < acount(roots); i++) {
        bool model = !strcmp(roots[i], "save") || !strcmp(roots[i], "system") ||
                     !strcmp(roots[i], "ev");
        bool found = false;
        for (int d = 0; d < acount(defined); d++) {
            found |= !strcmp(defined[d], roots[i]);
        }
        if (!model && !found) {
            ui_source_list_add(&out, roots[i]);
        }
    }
    free_names(roots);
    free_names(defined);
    return out;
}

static void add_element(yyjson_mut_doc *doc, yyjson_mut_val *array, const ui_source *source, int index)
{
    const ui_source_element *e = &source->elements[index];
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_int(doc, object, "index", index);
    yyjson_mut_obj_add_strcpy(doc, object, "tag", e->tag);
    yyjson_mut_obj_add_int(doc, object, "line", e->line);
    yyjson_mut_obj_add_int(doc, object, "parent", e->parent);
    yyjson_mut_val *attributes = yyjson_mut_obj_add_obj(doc, object, "attributes");
    for (int a = 0; a < acount(e->attributes); a++) {
        char *name = slice(source, e->attributes[a].name_start, e->attributes[a].name_end);
        char *value = ui_source_attribute_value(source, index, name);
        yyjson_mut_obj_add(attributes, yyjson_mut_strcpy(doc, name), yyjson_mut_strcpy(doc, value));
        free(value);
        free(name);
    }
}

static void add_rule(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    const ui_source *source,
    const ui_source_rule *rule
)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "selector", rule->selector);
    yyjson_mut_obj_add_int(doc, object, "line", rule->line);
    yyjson_mut_obj_add_int(doc, object, "parent", rule->parent);
    yyjson_mut_val *declarations = yyjson_mut_obj_add_obj(doc, object, "declarations");
    for (int d = 0; d < acount(rule->declarations); d++) {
        const ui_source_declaration *declaration = &rule->declarations[d];
        char *property = slice(source, declaration->property_start, declaration->property_end);
        char *value = slice(source, declaration->value_start, declaration->value_end);
        yyjson_mut_obj_add(declarations, yyjson_mut_strcpy(doc, property), yyjson_mut_strcpy(doc, value));
        free(property);
        free(value);
    }
}

/* Each <use ui> with the props it passes through data-alias-, and each
 * <slot/>. */
static void add_widgets_and_slots(yyjson_mut_doc *doc, yyjson_mut_val *result, const ui_source *source)
{
    yyjson_mut_val *widgets = yyjson_mut_obj_add_arr(doc, result, "widgets");
    yyjson_mut_val *slots = yyjson_mut_obj_add_arr(doc, result, "slots");
    for (int i = 0; i < acount(source->elements); i++) {
        const ui_source_element *e = &source->elements[i];
        if (!strcmp(e->tag, "slot")) {
            yyjson_mut_val *slot = yyjson_mut_arr_add_obj(doc, slots);
            yyjson_mut_obj_add_int(doc, slot, "element", i);
            yyjson_mut_obj_add_int(doc, slot, "line", e->line);
        }
        char *name = strcmp(e->tag, "use")
                       ? NULL
                       : ui_source_attribute_value(source, i, "ui");
        if (!name) {
            continue;
        }
        yyjson_mut_val *widget = yyjson_mut_arr_add_obj(doc, widgets);
        yyjson_mut_obj_add_strcpy(doc, widget, "name", name);
        yyjson_mut_obj_add_int(doc, widget, "element", i);
        yyjson_mut_obj_add_int(doc, widget, "line", e->line);
        yyjson_mut_val *passed = yyjson_mut_obj_add_obj(doc, widget, "props");
        for (int a = 0; a < acount(e->attributes); a++) {
            char *attribute = slice(
                source,
                e->attributes[a].name_start,
                e->attributes[a].name_end
            );
            char *value = !strncmp(attribute, "data-alias-", 11)
                            ? ui_source_attribute_value(source, i, attribute)
                            : NULL;
            if (value) {
                yyjson_mut_obj_add(
                    passed,
                    yyjson_mut_strcpy(doc, attribute + 11),
                    yyjson_mut_strcpy(doc, value)
                );
            }
            free(value);
            free(attribute);
        }
        free(name);
    }
}

static void add_events(yyjson_mut_doc *doc, yyjson_mut_val *result, const char *text)
{
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, result, "events");
    ui_document document = { 0 };
    char error[160];
    if (!ui_document_read(text, NULL, NULL, &document, error, sizeof error)) {
        return;
    }
    ui_document_event *events = NULL;
    ui_document_events(document.rml, &events);
    for (int i = 0; i < acount(events); i++) {
        yyjson_mut_val *event = yyjson_mut_arr_add_obj(doc, array);
        yyjson_mut_obj_add_strcpy(doc, event, "name", events[i].name);
        yyjson_mut_obj_add_strcpy(doc, event, "element", events[i].element);
        if (events[i].key) {
            yyjson_mut_obj_add_strcpy(doc, event, "key", events[i].key);
        }
        yyjson_mut_obj_add_bool(doc, event, "input", events[i].input);
    }
    ui_document_events_free(events);
    ui_document_free(&document);
}

/* The custom properties, classes and save keys the asset names. */
static void add_symbols(yyjson_mut_doc *doc, yyjson_mut_val *result, const ui_source *source)
{
    ui_source_symbol *symbols = NULL;
    ui_source_symbols(source, &symbols);
    char **tokens = NULL, **declared = NULL, **classes = NULL, **styled = NULL,
         **keys = NULL;
    for (int i = 0; i < acount(symbols); i++) {
        const ui_source_symbol *s = &symbols[i];
        char ***list = s->kind == UI_SYMBOL_VARIABLE
                         ? (s->definition ? &declared : &tokens)
                     : s->kind == UI_SYMBOL_CLASS
                         ? (s->definition ? &styled : &classes)
                     : s->kind == UI_SYMBOL_SAVE_KEY ? &keys
                                                     : NULL;
        if (list) {
            ui_source_list_add(list, s->name);
        }
    }
    afree(symbols);
    add_names(doc, result, "tokens", tokens);
    add_names(doc, result, "declared_tokens", declared);
    add_names(doc, result, "classes", classes);
    add_names(doc, result, "styled_classes", styled);
    add_names(doc, result, "save_keys", keys);
    free_names(tokens);
    free_names(declared);
    free_names(classes);
    free_names(styled);
    free_names(keys);
}

void automation_ui_inspect(const char *text, yyjson_mut_doc *doc, yyjson_mut_val *result)
{
    ui_source *source = ui_source_parse(text, false);
    yyjson_mut_val *blocks = yyjson_mut_obj_add_arr(doc, result, "blocks");
    for (int i = 0; i < acount(source->blocks); i++) {
        yyjson_mut_val *block = yyjson_mut_arr_add_obj(doc, blocks);
        yyjson_mut_obj_add_str(
            doc,
            block,
            "block",
            BLOCK_NAMES[source->blocks[i].language]
        );
        yyjson_mut_obj_add_int(doc, block, "line", source->blocks[i].line);
    }
    yyjson_mut_val *elements = yyjson_mut_obj_add_arr(doc, result, "elements");
    for (int i = 0; i < acount(source->elements); i++) {
        add_element(doc, elements, source, i);
    }
    yyjson_mut_val *rules = yyjson_mut_obj_add_arr(doc, result, "rules");
    for (int i = 0; i < acount(source->rules); i++) {
        add_rule(doc, rules, source, &source->rules[i]);
    }
    char **read = props(source);
    add_names(doc, result, "props", read);
    free_names(read);
    add_events(doc, result, text);
    add_widgets_and_slots(doc, result, source);
    add_symbols(doc, result, source);
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(doc, result, "diagnostics");
    syntax_diagnostics(source, doc, diagnostics);
    ui_source_free(source);
}

/* ---------- References ---------- */

/* The symbols a findReferences request names by these keys. */
static const struct
{
    const char *key;
    ui_source_symbol_kind kind;
} UI_SYMBOL_KEYS[] = {
    { "ui_class", UI_SYMBOL_CLASS },
    { "token", UI_SYMBOL_VARIABLE },
    { "save_key", UI_SYMBOL_SAVE_KEY },
};

bool automation_ui_references_requested(yyjson_val *request)
{
    for (size_t i = 0; i < sizeof UI_SYMBOL_KEYS / sizeof *UI_SYMBOL_KEYS; i++) {
        if (yyjson_obj_get(request, UI_SYMBOL_KEYS[i].key)) {
            return true;
        }
    }
    return false;
}

static void add_locations(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    const ui_source_location *locations,
    bool definitions,
    size_t prefix
)
{
    for (int i = 0; i < acount(locations); i++) {
        if (locations[i].definition != definitions) {
            continue;
        }
        yyjson_mut_val *place = yyjson_mut_arr_add_obj(doc, array);
        yyjson_mut_obj_add_strcpy(doc, place, "path", locations[i].path + prefix);
        yyjson_mut_obj_add_int(doc, place, "line", locations[i].line);
        yyjson_mut_obj_add_int(doc, place, "column", locations[i].column);
    }
}

int automation_ui_references(
    const char *root,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t error_size
)
{
    ui_source_symbol symbol = { .kind = UI_SYMBOL_NONE };
    const char *key = NULL;
    for (size_t i = 0; i < sizeof UI_SYMBOL_KEYS / sizeof *UI_SYMBOL_KEYS; i++) {
        const char *name = yyjson_get_str(
            yyjson_obj_get(request, UI_SYMBOL_KEYS[i].key)
        );
        if (name && name[0] && strlen(name) < sizeof symbol.name) {
            symbol.kind = UI_SYMBOL_KEYS[i].kind;
            snprintf(symbol.name, sizeof symbol.name, "%s", name);
            key = UI_SYMBOL_KEYS[i].key;
        }
    }
    if (!key) {
        return failf(error, error_size, CHIBI_AUTOMATION_INVALID_REQUEST, "ui_class, token and save_key each take a name");
    }
    char content[1024];
    snprintf(content, sizeof content, "%s/content", root);
    ui_source_project project = { 0 };
    ui_source_project_scan(&project, content);
    ui_source_location *found = NULL;
    ui_source_references(&project, &symbol, &found);
    yyjson_mut_val *named = yyjson_mut_obj_add_obj(doc, result, "symbol");
    yyjson_mut_obj_add_str(doc, named, "kind", key);
    yyjson_mut_obj_add_strcpy(doc, named, "name", symbol.name);
    size_t prefix = strlen(root) + 1;
    add_locations(doc, yyjson_mut_obj_add_arr(doc, result, "definitions"), found, true, prefix);
    add_locations(doc, yyjson_mut_obj_add_arr(doc, result, "uses"), found, false, prefix);
    yyjson_mut_obj_add_int(doc, result, "total", acount(found));
    /* Workstream I: what Studio's Text Editor shows on hover. */
    char *description = ui_source_describe(&project, &symbol);
    yyjson_mut_obj_add_strcpy(doc, result, "description", description);
    free(description);
    ui_source_locations_free(found);
    ui_source_project_free(&project);
    return CHIBI_AUTOMATION_OK;
}

/* ---------- Completion (workstream Q) ---------- */

/* The most items a completion lists; `total` counts them all. */
#define UI_COMPLETE_MAX 200

static const char *completion_kind(ui_source_completion_kind kind)
{
    switch (kind) {
        case UI_COMPLETE_TAG: return "tag";
        case UI_COMPLETE_ATTRIBUTE: return "attribute";
        case UI_COMPLETE_WIDGET: return "widget";
        case UI_COMPLETE_CLASS: return "class";
        case UI_COMPLETE_VARIABLE: return "custom_property";
        case UI_COMPLETE_PROPERTY: return "property";
        case UI_COMPLETE_SAVE_KEY: return "save_key";
    }
    return "unknown";
}

/* The project's ui assets and stylesheets, with the properties RmlUi
 * registers, as Studio's Text Editor completes from them (ui_code_scan). */
static void scan_project(ui_source_project *project, const char *content_root)
{
    ui_source_project_scan(project, content_root);
    rml_backend *backend = rml_backend_create(64, 64, 1);
    char **names = NULL;
    ui_check_property_names(&names);
    rml_backend_destroy(backend);
    for (int i = 0; i < acount(names); i++) {
        ui_source_list_add(&project->properties, names[i]);
    }
    free_names(names);
}

static void add_position(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *key,
    const ui_source *source,
    int offset
)
{
    int line, column;
    ui_source_position(source, offset, &line, &column);
    yyjson_mut_val *at = yyjson_mut_obj_add_obj(doc, object, key);
    yyjson_mut_obj_add_int(doc, at, "line", line);
    yyjson_mut_obj_add_int(doc, at, "column", column);
}

int automation_ui_complete(
    const char *content_root,
    const char *text,
    yyjson_val *at,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t error_size
)
{
    yyjson_val *line = yyjson_obj_get(at, "line"),
               *column = yyjson_obj_get(at, "column");
    if (!yyjson_is_obj(at) || !yyjson_is_int(line) || !yyjson_is_int(column) ||
        yyjson_get_int(line) < 1 || yyjson_get_int(column) < 1) {
        return failf(error, error_size, CHIBI_AUTOMATION_INVALID_REQUEST, "complete takes a 1-based line and column");
    }
    ui_source *source = ui_source_parse(text, false);
    int offset = ui_source_offset(source, yyjson_get_int(line), yyjson_get_int(column));
    ui_source_project project = { 0 };
    scan_project(&project, content_root);
    ui_source_completions found = { 0 };
    ui_source_complete(source, offset, &project, &found);
    yyjson_mut_val *completion = yyjson_mut_obj_add_obj(doc, result, "completion");
    add_position(doc, completion, "at", source, offset);
    add_position(doc, completion, "from", source, found.start);
    yyjson_mut_val *items = yyjson_mut_obj_add_arr(doc, completion, "items");
    for (int i = 0; i < acount(found.items) && i < UI_COMPLETE_MAX; i++) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, items);
        yyjson_mut_obj_add_strcpy(doc, item, "label", found.items[i].label);
        yyjson_mut_obj_add_str(doc, item, "kind", completion_kind(found.items[i].kind));
    }
    yyjson_mut_obj_add_int(doc, completion, "total", acount(found.items));
    ui_source_completions_free(&found);
    ui_source_project_free(&project);
    ui_source_free(source);
    return CHIBI_AUTOMATION_OK;
}
