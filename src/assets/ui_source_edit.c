/* The ui language service's edits. Each operation changes a block's value
 * in its own offsets, then turns those changes into the file's: in a literal
 * block line by line with its indentation, and otherwise by writing the
 * whole scalar again, as the asset writer would. */
#include "assets/ui_source.h"
#include "assets/ui_source_internal.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* The block's indentation step when no nesting shows one. */
#define DEFAULT_INDENT 2

typedef struct editing
{
    const ui_source *source;
    const block_map *map;
    text_edit *changes; /* in the value's offsets, their texts owned */
    char *error;
    size_t error_size;
} editing;

static bool fail(editing *e, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(e->error, e->error_size, format, args);
    va_end(args);
    return false;
}

static bool begin(editing *e, const ui_source *source, ui_source_language language, char *error, size_t size)
{
    *e = (editing){ .source = source,
                    .map = ui_source_map(source, language),
                    .error = error,
                    .error_size = size };
    return e->map ||
           fail(e, "the asset has no %s block", language == UI_SOURCE_RML ? "rml" : "rcss");
}

static void change(editing *e, int start, int end, const char *text)
{
    assert(start >= 0 && start <= end && end <= e->map->length);
    apush(e->changes, ((text_edit){ start, end, xstrdup(text) }));
}

/* Text built up in a support/array.h array, NUL-terminated by `take`. */
static void put(char **out, const char *text, size_t length)
{
    int at = acount(*out);
    asetlen(*out, at + (int)length);
    memcpy(*out + at, text, length);
}

static void put_text(char **out, const char *text)
{
    put(out, text, strlen(text));
}

static void put_spaces(char **out, int count)
{
    for (int i = 0; i < count; i++) {
        apush(*out, ' ');
    }
}

static char *take(char **out)
{
    apush(*out, '\0');
    char *text = xstrdup(*out);
    afree(*out);
    return text;
}

static void change_built(editing *e, int start, int end, char **built)
{
    char *text = take(built);
    change(e, start, end, text);
    free(text);
}

static void discard(editing *e)
{
    ui_source_edits_free(e->changes);
    e->changes = NULL;
}

void ui_source_edits_free(text_edit *edits)
{
    for (int i = 0; i < acount(edits); i++) {
        free((char *)edits[i].text);
    }
    afree(edits);
}

/* ---------- Into the file ---------- */

static bool begins_line(const block_map *m, int at)
{
    return at == 0 || m->value[at - 1] == '\n';
}

/* The file offset of the start of the line holding value offset `at`,
 * before its indentation. */
static int file_line_start(const ui_source *source, const block_map *m, int at)
{
    int offset = ui_source_file_offset(m, at);
    while (offset > 0 && source->text[offset - 1] != '\n') {
        offset--;
    }
    return offset;
}

/* One change of a literal block, with the block's indentation put before
 * each line of content the change starts. */
static void literal_edit(const ui_source *source, const block_map *m, const text_edit *c, text_edit **out)
{
    bool opens = begins_line(m, c->start), at_line = opens;
    char *text = NULL;
    for (const char *p = c->text; *p; p++) {
        if (at_line && *p != '\n') {
            put_spaces(&text, m->indent);
        }
        apush(text, *p);
        at_line = *p == '\n';
    }
    bool closes = c->text[0] ? at_line : opens;
    int start = opens ? file_line_start(source, m, c->start)
                      : ui_source_file_offset(m, c->start);
    int end = ui_source_file_offset(m, c->end);
    if (begins_line(m, c->end) && closes) {
        end = file_line_start(source, m, c->end);
    } else if (!begins_line(m, c->end) && closes && c->end < m->length && m->value[c->end] != '\n') {
        put_spaces(&text, m->indent);
    }
    apush(*out, ((text_edit){ start, end, take(&text) }));
}

/* The scalar YAML writes for `value`, after its key's ": ". */
static char *yaml_scalar(const char *value)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "v", value);
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    char error[160];
    char *yaml = asset_text_to_disk(json, error, sizeof error);
    free(json);
    assert(yaml && !strncmp(yaml, "v: ", 3));
    size_t length = strlen(yaml);
    char *scalar = xmalloc(length);
    snprintf(scalar, length - 3, "%s", yaml + 3);
    free(yaml);
    return scalar;
}

static int change_order(const void *a, const void *b)
{
    const text_edit *x = a, *y = b;
    return x->start != y->start ? (x->start > y->start) - (x->start < y->start)
                                : (x->end > y->end) - (x->end < y->end);
}

/* Sorts the changes and joins those that touch, so none share an offset. */
static void join_changes(editing *e)
{
    text_edit *c = e->changes;
    qsort(c, (size_t)acount(c), sizeof *c, change_order);
    int kept = 0;
    for (int i = 0; i < acount(c); i++) {
        if (!kept || c[kept - 1].end < c[i].start) {
            c[kept++] = c[i];
            continue;
        }
        assert(c[kept - 1].end == c[i].start);
        size_t size = strlen(c[kept - 1].text) + strlen(c[i].text) + 1;
        char *joined = xmalloc(size);
        snprintf(joined, size, "%s%s", c[kept - 1].text, c[i].text);
        free((char *)c[kept - 1].text);
        free((char *)c[i].text);
        c[kept - 1].text = joined;
        c[kept - 1].end = c[i].end;
    }
    asetlen(e->changes, kept);
}

/* Appends the file's edits for the changes. */
static bool finish(editing *e, text_edit **out)
{
    const block_map *m = e->map;
    join_changes(e);
    if (m->style == SCALAR_LITERAL) {
        for (int i = 0; i < acount(e->changes); i++) {
            literal_edit(e->source, m, &e->changes[i], out);
        }
    } else if (acount(e->changes)) {
        char *value = text_edits_apply(m->value, e->changes, acount(e->changes));
        apush(*out, ((text_edit){ m->scalar_start, m->scalar_end, yaml_scalar(value) }));
        free(value);
    }
    discard(e);
    return true;
}

/* ---------- Elements and attributes ---------- */

static const ui_source_element *element_of(editing *e, int index)
{
    return index >= 0 && index < acount(e->map->elements) ? &e->map->elements[index]
                                                          : NULL;
}

static const ui_source_attribute *attribute_of(
    editing *e,
    const ui_source_element *element,
    const char *name
)
{
    size_t length = strlen(name);
    for (int i = 0; i < acount(element->attributes); i++) {
        const ui_source_attribute *a = &element->attributes[i];
        if ((size_t)(a->name_end - a->name_start) == length &&
            !strncmp(e->map->value + a->name_start, name, length)) {
            return a;
        }
    }
    return NULL;
}

static bool valid_name(const char *name)
{
    if (!name || !name[0]) {
        return false;
    }
    for (const char *p = name; *p; p++) {
        if (!isalnum((unsigned char)*p) && !strchr("-_:.", *p)) {
            return false;
        }
    }
    return true;
}

/* name="value", with the value's double quotes as entities. */
static void put_attribute(char **out, const char *name, const char *value)
{
    put_text(out, name);
    put_text(out, "=\"");
    for (const char *p = value; *p; p++) {
        if (*p == '"') {
            put_text(out, "&quot;");
        } else {
            apush(*out, *p);
        }
    }
    apush(*out, '"');
}

/* Where a new attribute goes: after the last, or after the tag's name. */
static int attribute_end(const ui_source_element *element)
{
    int count = acount(element->attributes);
    return count ? element->attributes[count - 1].end
                 : element->start + 1 + (int)strlen(element->tag);
}

static void set_attribute(editing *e, const ui_source_element *element, const char *name, const char *value)
{
    const ui_source_attribute *a = attribute_of(e, element, name);
    char *text = NULL;
    if (!a) {
        apush(text, ' ');
    }
    put_attribute(&text, name, value);
    int start = a ? a->start : attribute_end(element);
    change_built(e, start, a ? a->end : start, &text);
}

/* The attribute with the space before it. */
static void remove_attribute(editing *e, const ui_source_attribute *a)
{
    int start = a->start;
    while (start > 0 && isspace((unsigned char)e->map->value[start - 1])) {
        start--;
    }
    change(e, start, a->end, "");
}

static bool element_error(editing *e, int element)
{
    return fail(e, "there is no element %d", element);
}

bool ui_source_set_attribute(
    const ui_source *source,
    int element,
    const char *name,
    const char *value,
    text_edit **edits,
    char *error,
    size_t error_size
)
{
    editing e;
    if (!begin(&e, source, UI_SOURCE_RML, error, error_size)) {
        return false;
    }
    const ui_source_element *el = element_of(&e, element);
    if (!el) {
        return element_error(&e, element);
    }
    if (!valid_name(name)) {
        return fail(&e, "\"%s\" is not an attribute name", name ? name : "");
    }
    set_attribute(&e, el, name, value);
    return finish(&e, edits);
}

bool ui_source_remove_attribute(
    const ui_source *source,
    int element,
    const char *name,
    text_edit **edits,
    char *error,
    size_t error_size
)
{
    editing e;
    if (!begin(&e, source, UI_SOURCE_RML, error, error_size)) {
        return false;
    }
    const ui_source_element *el = element_of(&e, element);
    if (!el) {
        return element_error(&e, element);
    }
    const ui_source_attribute *a = attribute_of(&e, el, name);
    if (!a) {
        return fail(&e, "<%s> has no %s attribute", el->tag, name);
    }
    remove_attribute(&e, a);
    return finish(&e, edits);
}

/* ---------- Classes ---------- */

/* The class `name` in the class attribute's value: its start, or -1. */
static int class_start(editing *e, const ui_source_attribute *a, const char *name, int *end)
{
    const char *v = e->map->value;
    int length = (int)strlen(name);
    for (int i = a->value_start; i < a->value_end;) {
        while (i < a->value_end && isspace((unsigned char)v[i])) {
            i++;
        }
        int start = i;
        while (i < a->value_end && !isspace((unsigned char)v[i])) {
            i++;
        }
        if (i - start == length && !strncmp(v + start, name, (size_t)length)) {
            *end = i;
            return start;
        }
    }
    return -1;
}

static bool blank_range(const char *v, int start, int end)
{
    for (int i = start; i < end; i++) {
        if (!isspace((unsigned char)v[i])) {
            return false;
        }
    }
    return true;
}

bool ui_source_add_class(
    const ui_source *source,
    int element,
    const char *name,
    text_edit **edits,
    char *error,
    size_t error_size
)
{
    editing e;
    if (!begin(&e, source, UI_SOURCE_RML, error, error_size)) {
        return false;
    }
    const ui_source_element *el = element_of(&e, element);
    if (!el) {
        return element_error(&e, element);
    }
    if (!valid_name(name)) {
        return fail(&e, "\"%s\" is not a class name", name ? name : "");
    }
    const ui_source_attribute *a = attribute_of(&e, el, "class");
    int end;
    if (!a || a->value_start < 0) {
        set_attribute(&e, el, "class", name);
    } else if (class_start(&e, a, name, &end) < 0) {
        bool empty = blank_range(e.map->value, a->value_start, a->value_end);
        char text[160];
        snprintf(text, sizeof text, "%s%s", empty ? "" : " ", name);
        change(&e, empty ? a->value_start : a->value_end, a->value_end, text);
    }
    return finish(&e, edits);
}

bool ui_source_remove_class(
    const ui_source *source,
    int element,
    const char *name,
    text_edit **edits,
    char *error,
    size_t error_size
)
{
    editing e;
    if (!begin(&e, source, UI_SOURCE_RML, error, error_size)) {
        return false;
    }
    const ui_source_element *el = element_of(&e, element);
    if (!el) {
        return element_error(&e, element);
    }
    const ui_source_attribute *a = attribute_of(&e, el, "class");
    int end = 0,
        start = a && a->value_start >= 0 ? class_start(&e, a, name, &end) : -1;
    if (start < 0) {
        return fail(&e, "<%s> has no class %s", el->tag, name);
    }
    const char *v = e.map->value;
    if (blank_range(v, a->value_start, start) && blank_range(v, end, a->value_end)) {
        remove_attribute(&e, a);
    } else if (end < a->value_end && isspace((unsigned char)v[end])) {
        change(&e, start, end + 1, "");
    } else {
        change(&e, start - 1, end, "");
    }
    return finish(&e, edits);
}

/* ---------- Inline style ---------- */

typedef struct declaration_span
{
    int start, end;             /* through its ';' when it has one */
    int value_start, value_end; /* trimmed */
} declaration_span;

static int trim_start(const char *v, int start, int end)
{
    while (start < end && isspace((unsigned char)v[start])) {
        start++;
    }
    return start;
}

static int trim_end(const char *v, int start, int end)
{
    while (end > start && isspace((unsigned char)v[end - 1])) {
        end--;
    }
    return end;
}

/* The declaration of `property` in a style attribute's value. */
static bool find_declaration(
    const char *v,
    const ui_source_attribute *a,
    const char *property,
    declaration_span *out
)
{
    size_t length = strlen(property);
    for (int i = a->value_start; i < a->value_end;) {
        int start = trim_start(v, i, a->value_end), stop = start;
        while (stop < a->value_end && v[stop] != ';') {
            stop++;
        }
        const char *colon = memchr(v + start, ':', (size_t)(stop - start));
        int name_end = colon ? trim_end(v, start, (int)(colon - v)) : start;
        if (colon && (size_t)(name_end - start) == length &&
            !strncmp(v + start, property, length)) {
            int value = (int)(colon - v) + 1;
            *out = (declaration_span){ start,
                                       stop + (stop < a->value_end),
                                       trim_start(v, value, stop),
                                       trim_end(v, value, stop) };
            return true;
        }
        i = stop + 1;
    }
    return false;
}

static void add_declaration(editing *e, const ui_source_attribute *a, const char *property, const char *value)
{
    const char *v = e->map->value;
    int end = trim_end(v, a->value_start, a->value_end);
    bool empty = end == a->value_start, closed = !empty && v[end - 1] == ';';
    char text[320];
    snprintf(
        text,
        sizeof text,
        "%s%s: %s%s",
        empty    ? ""
        : closed ? " "
                 : "; ",
        property,
        value,
        empty || closed ? ";" : ""
    );
    change(e, empty ? a->value_start : end, empty ? a->value_end : end, text);
}

/* The declaration with the space after it, or before it when it is last. */
static void remove_declaration(editing *e, const ui_source_attribute *a, const declaration_span *d)
{
    const char *v = e->map->value;
    int next = trim_start(v, d->end, a->value_end);
    bool first = blank_range(v, a->value_start, d->start), last = next == a->value_end;
    if (first && last) {
        remove_attribute(e, a);
    } else if (last) {
        change(e, trim_end(v, a->value_start, d->start), d->end, "");
    } else {
        change(e, d->start, next, "");
    }
}

bool ui_source_set_style(
    const ui_source *source,
    int element,
    const char *property,
    const char *value,
    text_edit **edits,
    char *error,
    size_t error_size
)
{
    editing e;
    if (!begin(&e, source, UI_SOURCE_RML, error, error_size)) {
        return false;
    }
    const ui_source_element *el = element_of(&e, element);
    if (!el) {
        return element_error(&e, element);
    }
    if (!valid_name(property) || (value && strpbrk(value, ";\""))) {
        return fail(
            &e,
            "\"%s: %s\" is not a style declaration",
            property ? property : "",
            value ? value : ""
        );
    }
    const ui_source_attribute *a = attribute_of(&e, el, "style");
    declaration_span d;
    bool found = a && a->value_start >= 0 &&
                 find_declaration(e.map->value, a, property, &d);
    if (!value && !found) {
        return fail(&e, "<%s> has no style property %s", el->tag, property);
    }
    if (!value) {
        remove_declaration(&e, a, &d);
    } else if (found) {
        change(&e, d.value_start, d.value_end, value);
    } else if (a && a->value_start >= 0) {
        add_declaration(&e, a, property, value);
    } else {
        char text[320];
        snprintf(text, sizeof text, "%s: %s;", property, value);
        set_attribute(&e, el, "style", text);
    }
    return finish(&e, edits);
}

/* ---------- Structure ---------- */

static int line_begin(const block_map *m, int at)
{
    while (at > 0 && m->value[at - 1] != '\n') {
        at--;
    }
    return at;
}

/* One past the line break ending the line that holds `at`, or the end. */
static int next_line(const block_map *m, int at)
{
    while (at < m->length && m->value[at] != '\n') {
        at++;
    }
    return at < m->length ? at + 1 : at;
}

static int indent_at(const block_map *m, int at)
{
    int start = line_begin(m, at), end = start;
    while (m->value[end] == ' ' || m->value[end] == '\t') {
        end++;
    }
    return end - start;
}

/* Whether [start, end) has its lines to itself. */
static bool alone(const block_map *m, int start, int end)
{
    int after = next_line(m, end);
    return blank_range(m->value, line_begin(m, start), start) &&
           blank_range(m->value, end, after);
}

static bool element_alone(const block_map *m, const ui_source_element *element)
{
    return alone(m, element->start, element->end);
}

/* The indentation a child adds, as the markup's nested lines show it. */
static int indent_step(const block_map *m)
{
    int step = 0;
    for (int i = 0; i < acount(m->elements); i++) {
        const ui_source_element *child = &m->elements[i];
        if (child->parent < 0 || !element_alone(m, child)) {
            continue;
        }
        const ui_source_element *parent = &m->elements[child->parent];
        int difference = indent_at(m, child->start) - indent_at(m, parent->start);
        if (difference > 0 && (!step || difference < step) &&
            line_begin(m, child->start) != line_begin(m, parent->start)) {
            step = difference;
        }
    }
    return step ? step : DEFAULT_INDENT;
}

/* `markup` trimmed, with `indent` more spaces on each line after its first. */
static void put_indented(char **out, const char *markup, int indent)
{
    int start = trim_start(markup, 0, (int)strlen(markup));
    int end = trim_end(markup, start, (int)strlen(markup));
    for (int i = start; i < end; i++) {
        apush(*out, markup[i]);
        if (markup[i] == '\n' && i + 1 < end && markup[i + 1] != '\n') {
            put_spaces(out, indent);
        }
    }
}

/* A line of its own at `at`, the start of a line, for `markup`. */
static void insert_line(editing *e, int at, int indent, const char *markup)
{
    const block_map *m = e->map;
    bool open = at == m->length && m->length && m->value[m->length - 1] != '\n';
    char *text = NULL;
    if (open) {
        apush(text, '\n');
    }
    put_spaces(&text, indent);
    put_indented(&text, markup, indent);
    if (!open) {
        apush(text, '\n');
    }
    change_built(e, at, at, &text);
}

static void insert_inline(editing *e, int at, const char *markup)
{
    char *text = NULL;
    put_indented(&text, markup, 0);
    change_built(e, at, at, &text);
}

/* A self-closing element opened to hold `markup`. */
static void open_element(editing *e, const ui_source_element *el, const char *markup)
{
    const block_map *m = e->map;
    int slash = el->end - 2;
    assert(!strncmp(m->value + slash, "/>", 2));
    slash = trim_end(m->value, el->start, slash);
    char *text = NULL;
    apush(text, '>');
    if (element_alone(m, el)) {
        int indent = indent_at(m, el->start), child = indent + indent_step(m);
        apush(text, '\n');
        put_spaces(&text, child);
        put_indented(&text, markup, child);
        apush(text, '\n');
        put_spaces(&text, indent);
    } else {
        put_indented(&text, markup, 0);
    }
    put_text(&text, "</");
    put_text(&text, el->tag);
    apush(text, '>');
    change_built(e, slash, el->end, &text);
}

static int last_child(const block_map *m, int parent)
{
    int last = -1;
    for (int i = parent + 1; i < acount(m->elements); i++) {
        last = m->elements[i].parent == parent ? i : last;
    }
    return last;
}

static bool insert_child(editing *e, int target, const char *markup)
{
    const block_map *m = e->map;
    const ui_source_element *el = &m->elements[target];
    if (el->self_closing) {
        open_element(e, el, markup);
        return true;
    }
    int close = el->close_start;
    if (close == el->end) {
        return fail(e, "<%s> has no end tag", el->tag);
    }
    bool own_line = blank_range(m->value, line_begin(m, close), close) &&
                    line_begin(m, close) > line_begin(m, el->start);
    if (!own_line) {
        insert_inline(e, close, markup);
        return true;
    }
    int child = last_child(m, target);
    int indent = child >= 0 && element_alone(m, &m->elements[child])
                   ? indent_at(m, m->elements[child].start)
                   : indent_at(m, close) + indent_step(m);
    insert_line(e, line_begin(m, close), indent, markup);
    return true;
}

static bool insert(editing *e, int target, ui_source_place place, const char *markup)
{
    const block_map *m = e->map;
    if (!markup || blank_range(markup, 0, (int)strlen(markup))) {
        return fail(e, "the markup to insert is empty");
    }
    if (target == -1 && place != UI_SOURCE_INSIDE) {
        return fail(e, "only an element has a before and an after");
    }
    if (target == -1) {
        insert_line(e, m->length, 0, markup);
        return true;
    }
    const ui_source_element *el = element_of(e, target);
    if (!el) {
        return element_error(e, target);
    }
    if (place == UI_SOURCE_INSIDE) {
        return insert_child(e, target, markup);
    }
    bool before = place == UI_SOURCE_BEFORE;
    if (!element_alone(m, el)) {
        insert_inline(e, before ? el->start : el->end, markup);
        return true;
    }
    int at = before ? line_begin(m, el->start) : next_line(m, el->end);
    insert_line(e, at, indent_at(m, el->start), markup);
    return true;
}

bool ui_source_insert_element(
    const ui_source *source,
    int target,
    ui_source_place place,
    const char *markup,
    text_edit **edits,
    char *error,
    size_t error_size
)
{
    editing e;
    if (!begin(&e, source, UI_SOURCE_RML, error, error_size)) {
        return false;
    }
    if (!insert(&e, target, place, markup)) {
        discard(&e);
        return false;
    }
    return finish(&e, edits);
}

static void delete_element(editing *e, const ui_source_element *el)
{
    const block_map *m = e->map;
    bool whole = element_alone(m, el);
    change(e, whole ? line_begin(m, el->start) : el->start, whole ? next_line(m, el->end) : el->end, "");
}

bool ui_source_delete_element(const ui_source *source, int element, text_edit **edits, char *error, size_t error_size)
{
    editing e;
    if (!begin(&e, source, UI_SOURCE_RML, error, error_size)) {
        return false;
    }
    const ui_source_element *el = element_of(&e, element);
    if (!el) {
        return element_error(&e, element);
    }
    delete_element(&e, el);
    return finish(&e, edits);
}

/* The element's markup with the indentation of its first line taken from
 * the lines after it. */
static char *dedented(const block_map *m, const ui_source_element *el)
{
    int indent = element_alone(m, el) ? indent_at(m, el->start) : 0;
    char *text = NULL;
    for (int i = el->start; i < el->end; i++) {
        apush(text, m->value[i]);
        if (m->value[i] != '\n') {
            continue;
        }
        for (int k = 0; k < indent && m->value[i + 1] == ' '; k++) {
            i++;
        }
    }
    return take(&text);
}

static bool holds(const block_map *m, int ancestor, int element)
{
    for (int i = element; i >= 0; i = m->elements[i].parent) {
        if (i == ancestor) {
            return true;
        }
    }
    return false;
}

bool ui_source_move_element(
    const ui_source *source,
    int element,
    int target,
    ui_source_place place,
    text_edit **edits,
    char *error,
    size_t error_size
)
{
    editing e;
    if (!begin(&e, source, UI_SOURCE_RML, error, error_size)) {
        return false;
    }
    const ui_source_element *el = element_of(&e, element);
    if (!el || (target != -1 && !element_of(&e, target))) {
        return element_error(&e, el ? target : element);
    }
    if (target >= 0 && holds(e.map, element, target)) {
        return fail(&e, "<%s> cannot move beside or into itself", el->tag);
    }
    char *markup = dedented(e.map, el);
    delete_element(&e, el);
    bool inserted = insert(&e, target, place, markup);
    free(markup);
    if (!inserted) {
        discard(&e);
        return false;
    }
    return finish(&e, edits);
}

/* ---------- Blocks ---------- */

void ui_source_set_block(
    const ui_source *source,
    ui_source_language language,
    const char *value,
    text_edit **edits
)
{
    const block_map *m = ui_source_map(source, language);
    if (source->trees->stylesheet) {
        assert(m);
        apush(*edits, ((text_edit){ 0, (int)strlen(source->text), xstrdup(value) }));
        return;
    }
    char *scalar = yaml_scalar(value);
    if (m) {
        apush(*edits, ((text_edit){ m->scalar_start, m->scalar_end, scalar }));
        return;
    }
    int end = (int)strlen(source->text);
    bool closed = !end || source->text[end - 1] == '\n';
    char *text = NULL;
    put_text(&text, closed ? "" : "\n");
    put_text(&text, language == UI_SOURCE_RML ? "rml: " : "rcss: ");
    put_text(&text, scalar);
    apush(text, '\n');
    free(scalar);
    apush(*edits, ((text_edit){ end, end, take(&text) }));
}
