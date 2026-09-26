#include "automation/ui_edit.h"

#include "assets/ui_document.h"
#include "assets/ui_source.h"
#include "automation/controller.h"
#include "render/ui_check.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* The most operations one buildUi request applies. */
#define UI_EDIT_MAX_OPERATIONS 128

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

/* ---------- Element selectors ---------- */

/* Whether the element's class attribute holds `name`. */
static bool has_class(const char *classes, const char *name, size_t length)
{
    for (const char *p = classes; p && *p;) {
        p += strspn(p, " \t\n");
        size_t word = strcspn(p, " \t\n");
        if (word == length && !strncmp(p, name, length)) {
            return true;
        }
        p += word;
    }
    return false;
}

/* Whether the element matches a compound selector: a tag, #id and .class
 * parts, as "use#resume" or "div.card.wide". */
static bool matches(const ui_source_element *e, const char *selector)
{
    const char *p = selector;
    size_t tag = strcspn(p, "#.");
    if (tag && (strlen(e->tag) != tag || strncmp(e->tag, p, tag))) {
        return false;
    }
    for (p += tag; *p;) {
        char kind = *p++;
        size_t length = strcspn(p, "#.");
        bool ok = kind == '#'
                    ? e->id && strlen(e->id) == length && !strncmp(e->id, p, length)
                    : has_class(e->classes, p, length);
        if (!ok || !length) {
            return false;
        }
        p += length;
    }
    return true;
}

/* The element a request names: its index in the outline inspectUi lists,
 * or a selector exactly one element matches. */
static int find_element(const ui_source *source, yyjson_val *value, int *element, char *error, size_t size)
{
    int count = acount(source->elements);
    if (yyjson_is_int(value)) {
        int64_t index = yyjson_get_sint(value);
        *element = (int)index;
        return index >= 0 && index < count ? CHIBI_AUTOMATION_OK
                                           : failf(
                                                 error,
                                                 size,
                                                 CHIBI_AUTOMATION_INVALID_REQUEST,
                                                 "there is no element %lld",
                                                 (long long)index
                                             );
    }
    const char *selector = yyjson_get_str(value);
    if (!selector || !selector[0] || strpbrk(selector, " >+~[:")) {
        return failf(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "an element is an index or a selector such as \"#id\" or \"div.card\"");
    }
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (matches(&source->elements[i], selector)) {
            *element = i;
            found++;
        }
    }
    return found == 1 ? CHIBI_AUTOMATION_OK
                      : failf(
                            error,
                            size,
                            CHIBI_AUTOMATION_INVALID_REQUEST,
                            "\"%s\" matches %d elements, not one",
                            selector,
                            found
                        );
}

static int find_place(yyjson_val *op, ui_source_place *place, char *error, size_t size)
{
    const char *position = yyjson_get_str(yyjson_obj_get(op, "position"));
    *place = !position || !strcmp(position, "inside") ? UI_SOURCE_INSIDE
           : !strcmp(position, "before")              ? UI_SOURCE_BEFORE
                                                      : UI_SOURCE_AFTER;
    bool valid = !position || !strcmp(position, "inside") ||
                 !strcmp(position, "before") || !strcmp(position, "after");
    return valid
             ? CHIBI_AUTOMATION_OK
             : failf(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "position is inside, before or after");
}

/* ---------- Operations ---------- */

static const char *text_field(yyjson_val *op, const char *key)
{
    return yyjson_get_str(yyjson_obj_get(op, key));
}

static int missing(char *error, size_t size, const char *op, const char *field)
{
    return failf(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "%s needs %s", op, field);
}

/* An operation on one element: set or remove an attribute, a class or a
 * style property, or delete it. */
static int element_operation(
    const ui_source *source,
    yyjson_val *op,
    const char *name,
    text_edit **edits,
    char *error,
    size_t size
)
{
    int element = 0;
    int status = find_element(source, yyjson_obj_get(op, "element"), &element, error, size);
    if (status != CHIBI_AUTOMATION_OK) {
        return status;
    }
    const char *attribute = text_field(op, "name"), *value = text_field(op, "value");
    const char *class_name = text_field(op, "class"),
               *property = text_field(op, "property");
    bool ok;
    if (!strcmp(name, "set_attribute")) {
        if (!attribute || !value) {
            return missing(error, size, name, "name and value");
        }
        ok = ui_source_set_attribute(source, element, attribute, value, edits, error, size);
    } else if (!strcmp(name, "remove_attribute")) {
        if (!attribute) {
            return missing(error, size, name, "name");
        }
        ok = ui_source_remove_attribute(source, element, attribute, edits, error, size);
    } else if (!strcmp(name, "add_class") || !strcmp(name, "remove_class")) {
        if (!class_name) {
            return missing(error, size, name, "class");
        }
        ok = name[0] == 'a'
               ? ui_source_add_class(source, element, class_name, edits, error, size)
               : ui_source_remove_class(source, element, class_name, edits, error, size);
    } else if (!strcmp(name, "set_style") || !strcmp(name, "remove_style")) {
        if (!property || (name[0] == 's' && !value)) {
            return missing(error, size, name, name[0] == 's' ? "property and value" : "property");
        }
        ok = ui_source_set_style(source, element, property, name[0] == 's' ? value : NULL, edits, error, size);
    } else {
        ok = ui_source_delete_element(source, element, edits, error, size);
    }
    return ok ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_INVALID_REQUEST;
}

/* insert_element and move_element: the target is an element, or the markup
 * itself when omitted. */
static int structure_operation(
    const ui_source *source,
    yyjson_val *op,
    const char *name,
    text_edit **edits,
    char *error,
    size_t size
)
{
    int target = -1, element = 0;
    ui_source_place place;
    yyjson_val *selector = yyjson_obj_get(op, "target");
    int status = find_place(op, &place, error, size);
    if (status == CHIBI_AUTOMATION_OK && selector && !yyjson_is_null(selector)) {
        status = find_element(source, selector, &target, error, size);
    }
    bool move = !strcmp(name, "move_element");
    if (status == CHIBI_AUTOMATION_OK && move) {
        status = find_element(source, yyjson_obj_get(op, "element"), &element, error, size);
    }
    if (status != CHIBI_AUTOMATION_OK) {
        return status;
    }
    const char *markup = text_field(op, "markup");
    if (!move && !markup) {
        return missing(error, size, name, "markup");
    }
    bool ok = move
                ? ui_source_move_element(source, element, target, place, edits, error, size)
                : ui_source_insert_element(source, target, place, markup, edits, error, size);
    return ok ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_INVALID_REQUEST;
}

static int apply_operation(const ui_source *source, yyjson_val *op, text_edit **edits, char *error, size_t size)
{
    static const char *const ELEMENT_OPERATIONS[] = {
        "set_attribute", "remove_attribute", "add_class",      "remove_class",
        "set_style",     "remove_style",     "delete_element", NULL
    };
    const char *name = text_field(op, "op");
    if (!name) {
        return missing(error, size, "an operation", "op");
    }
    for (int i = 0; ELEMENT_OPERATIONS[i]; i++) {
        if (!strcmp(name, ELEMENT_OPERATIONS[i])) {
            return element_operation(source, op, name, edits, error, size);
        }
    }
    if (!strcmp(name, "insert_element") || !strcmp(name, "move_element")) {
        return structure_operation(source, op, name, edits, error, size);
    }
    if (strcmp(name, "set_rml") && strcmp(name, "set_rcss")) {
        return failf(error, size, CHIBI_AUTOMATION_INVALID_REQUEST, "unknown operation %s", name);
    }
    const char *text = text_field(op, "text");
    if (!text) {
        return missing(error, size, name, "text");
    }
    ui_source_set_block(source, strcmp(name, "set_rml") ? UI_SOURCE_RCSS : UI_SOURCE_RML, text, edits);
    return CHIBI_AUTOMATION_OK;
}

/* The edits of one operation, placed in the text they apply to. */
static void add_edits(
    yyjson_mut_doc *doc,
    yyjson_mut_val *changes,
    yyjson_val *op,
    const ui_source *source,
    const text_edit *edits
)
{
    yyjson_mut_val *change = yyjson_mut_arr_add_obj(doc, changes);
    yyjson_mut_obj_add_strcpy(doc, change, "op", text_field(op, "op"));
    yyjson_mut_val *list = yyjson_mut_obj_add_arr(doc, change, "edits");
    for (int i = 0; i < acount(edits); i++) {
        int line, column, end_line, end_column;
        ui_source_position(source, edits[i].start, &line, &column);
        ui_source_position(source, edits[i].end, &end_line, &end_column);
        yyjson_mut_val *edit = yyjson_mut_arr_add_obj(doc, list);
        yyjson_mut_obj_add_int(doc, edit, "line", line);
        yyjson_mut_obj_add_int(doc, edit, "column", column);
        yyjson_mut_obj_add_int(doc, edit, "end_line", end_line);
        yyjson_mut_obj_add_int(doc, edit, "end_column", end_column);
        yyjson_mut_obj_add_strcpy(doc, edit, "text", edits[i].text);
    }
}

int automation_ui_edit(
    const char *source,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char **edited,
    char *error,
    size_t error_size
)
{
    yyjson_val *operations = yyjson_obj_get(request, "operations");
    size_t count = yyjson_arr_size(operations);
    if (!yyjson_is_arr(operations) || !count || count > UI_EDIT_MAX_OPERATIONS) {
        return failf(
            error,
            error_size,
            CHIBI_AUTOMATION_INVALID_REQUEST,
            "operations must list 1 to %d edits",
            UI_EDIT_MAX_OPERATIONS
        );
    }
    yyjson_mut_val *changes = yyjson_mut_obj_add_arr(doc, result, "changes");
    char *text = xstrdup(source);
    int status = CHIBI_AUTOMATION_OK;
    size_t index, total;
    yyjson_val *op;
    yyjson_arr_foreach(operations, index, total, op)
    {
        ui_source *parsed = ui_source_parse(text, false);
        text_edit *edits = NULL;
        char why[200] = "";
        status = yyjson_is_obj(op)
                   ? apply_operation(parsed, op, &edits, why, sizeof why)
                   : failf(why, sizeof why, CHIBI_AUTOMATION_INVALID_REQUEST, "an operation is an object");
        if (status == CHIBI_AUTOMATION_OK) {
            add_edits(doc, changes, op, parsed, edits);
            char *next = text_edits_apply(text, edits, acount(edits));
            free(text);
            text = next;
        } else {
            snprintf(error, error_size, "operation %zu: %s", index, why);
        }
        ui_source_edits_free(edits);
        ui_source_free(parsed);
        if (status != CHIBI_AUTOMATION_OK) {
            break;
        }
    }
    if (status != CHIBI_AUTOMATION_OK) {
        free(text);
        return status;
    }
    *edited = text;
    return CHIBI_AUTOMATION_OK;
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
    ui_source_locations_free(found);
    ui_source_project_free(&project);
    return CHIBI_AUTOMATION_OK;
}
