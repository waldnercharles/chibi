/* Code intelligence for the Text Editor (ui_code.h): the language service and
 * RmlUi's check, turned from files into the part the editor shows. */
#include "editor/documents/ui_code.h"

#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "support/xalloc.h"
#include "ui/rml.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const PART_KEYS[UI_PART_COUNT] = { "rml", "rcss" };

ui_source_language ui_code_language(ui_part part)
{
    return part == UI_PART_RML ? UI_SOURCE_RML : UI_SOURCE_RCSS;
}

static ui_part language_part(ui_source_language language)
{
    return language == UI_SOURCE_RML ? UI_PART_RML : UI_PART_RCSS;
}

static bool ends_with(const char *text, const char *suffix)
{
    size_t length = strlen(text), size = strlen(suffix);
    return length >= size && !strcmp(text + length - size, suffix);
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool ui_code_content_root(const char *path, char *root, size_t size)
{
    const char *ui = NULL;
    for (const char *p = strstr(path, "/ui/"); p; p = strstr(p + 1, "/ui/")) {
        ui = p;
    }
    if (!ui) {
        return false;
    }
    snprintf(root, size, "%.*s", (int)(ui - path), path);
    return true;
}

void ui_code_scan(ui_source_project *project, const char *content_root)
{
    ui_source_project_scan(project, content_root);
    char **names = NULL;
    ui_check_property_names(&names);
    for (int i = 0; i < acount(names); i++) {
        ui_source_list_add(&project->properties, names[i]);
        free(names[i]);
    }
    afree(names);
}

bool ui_code_check(const ui_buffer *b, rml_context *context, ui_check_message **messages, char *error, size_t size)
{
    if (b->stylesheet) {
        return true;
    }
    /* Outside a ui folder, widgets and imports have nowhere to resolve. */
    char root[PATH_MAX], name[128];
    if (!ui_code_content_root(b->path, root, sizeof root)) {
        return true;
    }
    snprintf(name, sizeof name, "%s", base_name(b->path));
    name[strcspn(name, ".")] = '\0';
    char *text = ui_buffer_compose(b);
    bool ok = ui_check_in(context, root, name, text, messages, error, size);
    free(text);
    return ok;
}

/* ---------- The editor over a buffer ---------- */

static bool continuation(char c)
{
    return ((unsigned char)c & 0xc0) == 0x80;
}

/* What lies between the common start and end of two texts, on whole UTF-8
   characters. */
static ui_code_change difference(const char *from, int from_length, const char *to, int to_length)
{
    int start = 0, end = 0;
    while (start < from_length && start < to_length && from[start] == to[start]) {
        start++;
    }
    while (start > 0 && (continuation(from[start]) || continuation(to[start]))) {
        start--;
    }
    while (end < from_length - start && end < to_length - start &&
           from[from_length - 1 - end] == to[to_length - 1 - end]) {
        end++;
    }
    while (end > 0 && (continuation(from[from_length - end]) ||
                       continuation(to[to_length - end]))) {
        end--;
    }
    return (ui_code_change){ start, from_length - end, to_length - start - end };
}

/* The edit a change makes, its text owned. */
static text_edit edit_of(ui_code_change d, const char *to)
{
    char *insert = xmalloc((size_t)d.inserted + 1);
    memcpy(insert, to + d.start, (size_t)d.inserted);
    insert[d.inserted] = '\0';
    return (text_edit){ d.start, d.end, insert };
}

static bool unchanged(ui_code_change d)
{
    return d.start == d.end && !d.inserted;
}

ui_code_change ui_code_record(ui_buffer *b, ui_part part, kit_code_editor *e, double now, bool step)
{
    size_t length;
    const char *text = kit_code_text(e, &length);
    const char *old = ui_buffer_text(b, part);
    ui_code_change d = difference(old, (int)strlen(old), text, (int)length);
    if (!step) {
        ui_buffer_type(b, part, text, now);
        return d;
    }
    if (unchanged(d)) {
        return d;
    }
    text_edit edit = edit_of(d, text);
    ui_buffer_edit(b, part, &edit, 1);
    free((char *)edit.text);
    return d;
}

bool ui_code_follow(kit_code_editor *e, const char *text, bool land)
{
    size_t length;
    const char *shown = kit_code_text(e, &length);
    ui_code_change d = difference(shown, (int)length, text, (int)strlen(text));
    if (unchanged(d)) {
        return false;
    }
    text_edit edit = edit_of(d, text);
    kit_code_apply(e, &edit, 1);
    free((char *)edit.text);
    if (land) {
        kit_code_set_cursor(e, d.start + d.inserted);
    }
    return true;
}

bool ui_code_replay(ui_buffer *b, bool redo)
{
    if (redo ? !ui_buffer_can_redo(b) : !ui_buffer_can_undo(b)) {
        return false;
    }
    if (redo) {
        ui_buffer_redo(b);
    } else {
        ui_buffer_undo(b);
    }
    return true;
}

/* ---------- Diagnostics ---------- */

/* The 1-based line of the first place `text` holds `needle`; 1 without. */
static int line_naming(const char *text, const char *needle)
{
    const char *found = needle[0] ? strstr(text, needle) : NULL;
    int line = 1;
    for (const char *p = text; found && p < found; p++) {
        line += *p == '\n';
    }
    return line;
}

/* The first quoted text of a message, as an error names a widget. */
static void first_quoted(const char *message, char *out, size_t size)
{
    out[0] = '\0';
    const char *open = strpbrk(message, "'\"");
    const char *close = open ? strchr(open + 1, *open) : NULL;
    if (close) {
        snprintf(out, size, "%.*s", (int)(close - open - 1), open + 1);
    }
}

/* A message's first line: RmlUi follows a data expression's warning with
   the expression and a caret. */
static char *first_line(const char *prefix, const char *text)
{
    size_t size = strcspn(text, "\n") + strlen(prefix) + 1;
    char *line = xmalloc(size);
    snprintf(line, size, "%s%.*s", prefix, (int)strcspn(text, "\n"), text);
    return line;
}

static void add_marker(kit_code_marker **markers, int line, kit_code_severity severity, char *message)
{
    apush(*markers, ((kit_code_marker){ line > 0 ? line : 1, severity, message }));
}

/* A widget's or an import's message marks where the part names it. */
static void add_message(kit_code_marker **markers, const char *text, const ui_check_message *m)
{
    if (!m->source) {
        add_marker(markers, m->line, KIT_CODE_WARNING, first_line("", m->text));
        return;
    }
    char needle[256], prefix[160];
    if (m->part == UI_CHECK_WIDGETS) {
        snprintf(needle, sizeof needle, "ui=\"%s\"", m->source);
    } else {
        snprintf(needle, sizeof needle, "%s", base_name(m->source));
    }
    snprintf(prefix, sizeof prefix, "%s: ", base_name(m->source));
    add_marker(markers, line_naming(text, needle), KIT_CODE_WARNING, first_line(prefix, m->text));
}

static int by_line(const void *a, const void *b)
{
    const kit_code_marker *x = a, *y = b;
    return x->line != y->line
             ? (x->line > y->line) - (x->line < y->line)
             : (x->severity > y->severity) - (x->severity < y->severity);
}

void ui_code_markers(
    const ui_source *source,
    ui_part part,
    const ui_check_message *messages,
    const char *failure,
    kit_code_marker **markers
)
{
    for (int i = 0; i < acount(source->diagnostics); i++) {
        const ui_source_diagnostic *d = &source->diagnostics[i];
        add_marker(markers, d->line, KIT_CODE_ERROR, xstrdup(d->message));
    }
    for (int i = 0; i < acount(messages); i++) {
        const ui_check_message *m = &messages[i];
        ui_part about = m->part == UI_CHECK_RCSS ? UI_PART_RCSS : UI_PART_RML;
        if (about == part) {
            add_message(markers, source->text, m);
        }
    }
    if (failure && failure[0] && part == UI_PART_RML) {
        char name[128];
        first_quoted(failure, name, sizeof name);
        add_marker(markers, line_naming(source->text, name), KIT_CODE_ERROR, xstrdup(failure));
    }
    qsort(*markers, (size_t)acount(*markers), sizeof **markers, by_line);
}

void ui_code_markers_free(kit_code_marker *markers)
{
    for (int i = 0; i < acount(markers); i++) {
        free((char *)markers[i].message);
    }
    afree(markers);
}

/* ---------- Completion ---------- */

static const char *kind_detail(ui_source_completion_kind kind)
{
    switch (kind) {
        case UI_COMPLETE_TAG: return "element";
        case UI_COMPLETE_ATTRIBUTE: return "attribute";
        case UI_COMPLETE_WIDGET: return "widget";
        case UI_COMPLETE_CLASS: return "class";
        case UI_COMPLETE_VARIABLE: return "custom property";
        case UI_COMPLETE_PROPERTY: return "property";
        case UI_COMPLETE_SAVE_KEY: return "save key";
    }
    return NULL;
}

void ui_code_complete(
    const ui_source *source,
    int offset,
    const ui_source_project *project,
    ui_code_completions *out
)
{
    *out = (ui_code_completions){ 0 };
    ui_source_complete(source, offset, project, &out->found);
    out->start = out->found.start;
    for (int i = 0; i < acount(out->found.items) && i < UI_CODE_COMPLETIONS; i++) {
        const ui_source_completion *c = &out->found.items[i];
        apush(out->items, ((kit_code_completion){ c->label, kind_detail(c->kind), NULL }));
    }
}

void ui_code_completions_free(ui_code_completions *completions)
{
    ui_source_completions_free(&completions->found);
    afree(completions->items);
    *completions = (ui_code_completions){ 0 };
}

/* ---------- Hover ---------- */

/* The property a declaration at `offset` names, or false. */
static bool property_at(const ui_source *source, int offset, char *name, size_t size)
{
    for (int i = 0; i < acount(source->spans); i++) {
        const ui_source_span *span = &source->spans[i];
        const char *text = source->text + span->start;
        if (span->kind == UI_SOURCE_PROPERTY && span->start <= offset &&
            offset < span->end && strncmp(text, "--", 2)) {
            snprintf(name, size, "%.*s", span->end - span->start, text);
            return true;
        }
    }
    return false;
}

char *ui_code_hover(const ui_source *source, int offset, const ui_source_project *project)
{
    char *text = ui_source_hover(source, offset, project);
    char name[128], initial[128];
    bool inherited, shorthand;
    if (!text || !property_at(source, offset, name, sizeof name) ||
        !rml_property_describe(name, initial, sizeof initial, &inherited, &shorthand)) {
        return text;
    }
    char detail[256];
    if (shorthand) {
        snprintf(detail, sizeof detail, "\nA shorthand");
    } else {
        snprintf(
            detail,
            sizeof detail,
            "\nInitial %s%s",
            initial[0] ? initial : "none",
            inherited ? " \xc2\xb7 inherited" : ""
        );
    }
    size_t size = strlen(text) + strlen(detail) + 1;
    text = xrealloc(text, size);
    strcat(text, detail);
    return text;
}

/* ---------- Places ---------- */

static bool same_symbol(const ui_source_symbol *a, const ui_source_symbol *b)
{
    return a->kind == b->kind && !strcmp(a->name, b->name);
}

static bool same_file(const char *a, const char *b)
{
    char x[PATH_MAX], y[PATH_MAX];
    return !strcmp(a, b) || (realpath(a, x) && realpath(b, y) && !strcmp(x, y));
}

static void add_place(
    ui_code_place **places,
    const char *path,
    ui_part part,
    const ui_source *source,
    int offset,
    bool definition
)
{
    ui_code_place place = { .part = part, .offset = offset, .definition = definition };
    snprintf(place.path, sizeof place.path, "%s", path);
    int column;
    ui_source_position(source, offset, &place.line, &column);
    apush(*places, place);
}

/* The places the buffer's own parts name the symbol, as they stand. */
static void own_places(
    const ui_buffer *b,
    const ui_source_symbol *symbol,
    ui_code_place **definitions,
    ui_code_place **uses
)
{
    for (int p = 0; p < UI_PART_COUNT; p++) {
        const char *text = ui_buffer_text(b, (ui_part)p);
        if (!text) {
            continue;
        }
        ui_source *part = ui_source_parse_block(text, ui_code_language((ui_part)p));
        ui_source_symbol *found = NULL;
        ui_source_symbols(part, &found);
        for (int i = 0; i < acount(found); i++) {
            if (same_symbol(&found[i], symbol)) {
                add_place(
                    found[i].definition ? definitions : uses,
                    b->path,
                    (ui_part)p,
                    part,
                    found[i].start,
                    found[i].definition
                );
            }
        }
        afree(found);
        ui_source_free(part);
    }
}

/* A block's value as YAML means it, owned; NULL when the text is not a ui
   asset. */
static char *block_value(const char *text, ui_source_language language)
{
    char error[128];
    yyjson_doc *doc = asset_text_read(text, error, sizeof error);
    const char *value = yyjson_get_str(
        yyjson_obj_get(yyjson_doc_get_root(doc), PART_KEYS[language_part(language)])
    );
    char *copy = value ? xstrdup(value) : NULL;
    yyjson_doc_free(doc);
    return copy;
}

/* An occurrence of the symbol that defines it or not, as `definition`
   says, within [start, end). */
static bool occurrence(
    const ui_source_symbol *o,
    const ui_source_symbol *symbol,
    bool definition,
    int start,
    int end
)
{
    return same_symbol(o, symbol) && o->definition == definition &&
           o->start >= start && o->start < end;
}

/* How many such occurrences in `block` of `source` start before `before`. */
static int occurrences_before(
    const ui_source *source,
    const ui_source_block *block,
    const ui_source_symbol *symbol,
    bool definition,
    int before
)
{
    ui_source_symbol *found = NULL;
    ui_source_symbols(source, &found);
    int count = 0;
    for (int i = 0; i < acount(found); i++) {
        count += occurrence(&found[i], symbol, definition, block->start, before);
    }
    afree(found);
    return count;
}

/* Where the `nth` such occurrence in `source` starts, or -1. */
static int nth_occurrence(const ui_source *source, const ui_source_symbol *symbol, bool definition, int nth)
{
    ui_source_symbol *found = NULL;
    ui_source_symbols(source, &found);
    int at = -1;
    for (int i = 0; i < acount(found) && at < 0; i++) {
        if (occurrence(&found[i], symbol, definition, 0, INT_MAX) && nth-- == 0) {
            at = found[i].start;
        }
    }
    afree(found);
    return at;
}

/* A location in a ui asset's file as a place in one of its parts: the same
   occurrence of the symbol, counted in order, in the part's text. */
static void place_in_part(const ui_source_location *l, const ui_source_symbol *symbol, ui_code_place **places)
{
    char *text = text_file_read(l->path);
    ui_source *file = text ? ui_source_parse(text, false) : NULL;
    const ui_source_block *block = file ? ui_source_block_at(file, l->start) : NULL;
    char *value = block ? block_value(text, block->language) : NULL;
    if (value) {
        int nth = occurrences_before(file, block, symbol, l->definition, l->start);
        ui_source *part = ui_source_parse_block(value, block->language);
        int offset = nth_occurrence(part, symbol, l->definition, nth);
        add_place(places, l->path, language_part(block->language), part, offset < 0 ? 0 : offset, l->definition);
        ui_source_free(part);
    }
    free(value);
    ui_source_free(file);
    free(text);
}

static void other_places(
    const ui_buffer *b,
    const ui_source_symbol *symbol,
    const ui_source_project *project,
    ui_code_place **definitions,
    ui_code_place **uses
)
{
    ui_source_location *found = NULL;
    ui_source_references(project, symbol, &found);
    for (int i = 0; i < acount(found); i++) {
        const ui_source_location *l = &found[i];
        ui_code_place **places = l->definition ? definitions : uses;
        if (same_file(l->path, b->path)) {
            continue;
        }
        if (ends_with(l->path, ".rcss")) {
            ui_code_place place = { .part = UI_PART_RCSS,
                                    .offset = l->start,
                                    .line = l->line,
                                    .definition = l->definition };
            snprintf(place.path, sizeof place.path, "%s", l->path);
            apush(*places, place);
        } else if (symbol->kind == UI_SYMBOL_WIDGET && l->definition) {
            ui_code_place place = { .part = UI_PART_RML, .line = 1, .definition = true };
            snprintf(place.path, sizeof place.path, "%s", l->path);
            apush(*places, place);
        } else {
            place_in_part(l, symbol, places);
        }
    }
    ui_source_locations_free(found);
}

ui_source_symbol ui_code_places(
    const ui_buffer *b,
    const ui_source *source,
    int offset,
    const ui_source_project *project,
    ui_code_place **places
)
{
    ui_source_symbol symbol = ui_source_symbol_at(source, offset);
    if (symbol.kind == UI_SYMBOL_NONE) {
        return symbol;
    }
    ui_code_place *uses = NULL;
    own_places(b, &symbol, places, &uses);
    other_places(b, &symbol, project, places, &uses);
    for (int i = 0; i < acount(uses); i++) {
        apush(*places, uses[i]);
    }
    afree(uses);
    return symbol;
}

bool ui_code_definition(
    const ui_buffer *b,
    const ui_source *source,
    int offset,
    const ui_source_project *project,
    ui_code_place *definition
)
{
    ui_code_place *places = NULL;
    ui_source_symbol symbol = ui_code_places(b, source, offset, project, &places);
    bool found = false;
    for (int i = 0; i < acount(places) && !found; i++) {
        const ui_code_place *p = &places[i];
        bool here = !strcmp(p->path, b->path) && p->part == b->part &&
                    p->offset == symbol.start;
        if (p->definition && !here) {
            *definition = *p;
            found = true;
        }
    }
    afree(places);
    return found;
}
