/* The ui language service's project knowledge, completion, and definitions
 * and references. */
#include "assets/ui_source.h"
#include "assets/ui_source_internal.h"
#include "support/array.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* RmlUi's elements, with the widget and slot elements ui assets add. */
static const char *const TAGS[] = {
    "button", "br",     "col",    "colgroup", "div",      "h1",     "h2",
    "h3",     "handle", "hr",     "img",      "input",    "label",  "li",
    "ol",     "option", "p",      "panel",    "progress", "select", "slot",
    "span",   "tab",    "tabset", "table",    "tbody",    "td",     "textarea",
    "tfoot",  "thead",  "tr",     "ul",       "use",      NULL,
};

/* HTML's attributes RmlUi reads, its data bindings, and the ui asset's own. */
static const char *const ATTRIBUTES[] = {
    "attrs",
    "autofocus",
    "checked",
    "class",
    "cols",
    "data-alias-",
    "data-attr-",
    "data-checked",
    "data-class-",
    "data-event-blur",
    "data-event-change",
    "data-event-click",
    "data-event-dblclick",
    "data-event-focus",
    "data-event-keydown",
    "data-event-keyup",
    "data-event-mousedown",
    "data-event-mouseout",
    "data-event-mouseover",
    "data-event-mouseup",
    "data-event-submit",
    "data-for",
    "data-if",
    "data-keys",
    "data-rml",
    "data-style-",
    "data-value",
    "data-value-lazy",
    "data-visible",
    "disabled",
    "for",
    "id",
    "max",
    "maxlength",
    "min",
    "name",
    "placeholder",
    "rows",
    "selected",
    "size",
    "src",
    "step",
    "style",
    "type",
    "value",
    NULL,
};

/* ---------- Lists ---------- */

static int list_find(char **list, const char *name, bool *found)
{
    int low = 0, high = acount(list);
    while (low < high) {
        int middle = (low + high) / 2;
        int order = strcmp(list[middle], name);
        if (!order) {
            *found = true;
            return middle;
        }
        if (order < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    *found = false;
    return low;
}

void ui_source_list_add(char ***list, const char *name)
{
    bool found;
    int at = list_find(*list, name, &found);
    if (found) {
        return;
    }
    apush(*list, NULL);
    memmove(&(*list)[at + 1], &(*list)[at], sizeof **list * (size_t)(acount(*list) - 1 - at));
    (*list)[at] = xstrdup(name);
}

static void list_free(char **list)
{
    for (int i = 0; i < acount(list); i++) {
        free(list[i]);
    }
    afree(list);
}

/* ---------- Project ---------- */

static bool has_suffix(const char *name, const char *suffix)
{
    size_t length = strlen(name), size = strlen(suffix);
    return length > size && !strcmp(name + length - size, suffix);
}

static int name_order(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Appends the paths of the files under `folder` ending in `suffix`, sorted;
 * with `deep`, in its folders too. */
static void list_files(const char *folder, const char *suffix, bool deep, char ***paths)
{
    DIR *dir = opendir(folder);
    if (!dir) {
        return;
    }
    char **found = NULL;
    for (struct dirent *e; (e = readdir(dir));) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", folder, e->d_name);
        struct stat info;
        if (e->d_name[0] == '.' || stat(path, &info)) {
            continue;
        }
        if (S_ISDIR(info.st_mode) && deep) {
            list_files(path, suffix, deep, &found);
        } else if (S_ISREG(info.st_mode) && has_suffix(e->d_name, suffix)) {
            apush(found, xstrdup(path));
        }
    }
    closedir(dir);
    qsort(found, (size_t)acount(found), sizeof *found, name_order);
    for (int i = 0; i < acount(found); i++) {
        apush(*paths, found[i]);
    }
    afree(found);
}

static void add_file(ui_source_project *project, char *path, bool stylesheet)
{
    char *name = NULL;
    if (!stylesheet) {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        name = xstrdup(base);
        name[strlen(name) - strlen(".yaml")] = '\0';
        ui_source_list_add(&project->widgets, name);
    }
    apush(project->files, ((ui_source_file){ .path = path, .name = name }));
}

static void learn(ui_source_project *project, const ui_source *source)
{
    occurrence *found = NULL;
    ui_source_occurrences(source, &found);
    for (int i = 0; i < acount(found); i++) {
        char ***list = found[i].kind == UI_SYMBOL_CLASS    ? &project->classes
                     : found[i].kind == UI_SYMBOL_VARIABLE ? &project->variables
                     : found[i].kind == UI_SYMBOL_SAVE_KEY ? &project->save_keys
                                                           : NULL;
        if (list) {
            ui_source_list_add(list, found[i].name);
        }
    }
    afree(found);
}

/* The file's text, parsed; NULL when it cannot be read. */
static ui_source *parse_file(const ui_source_file *file)
{
    char *text = text_file_read(file->path);
    ui_source *source = text ? ui_source_parse(text, !file->name) : NULL;
    free(text);
    return source;
}

void ui_source_project_scan(ui_source_project *project, const char *content_root)
{
    char folder[1024];
    snprintf(folder, sizeof folder, "%s/ui", content_root);
    char **assets = NULL, **sheets = NULL;
    list_files(folder, ".yaml", false, &assets);
    list_files(folder, ".rcss", true, &sheets);
    for (int i = 0; i < acount(assets); i++) {
        add_file(project, assets[i], false);
    }
    for (int i = 0; i < acount(sheets); i++) {
        add_file(project, sheets[i], true);
    }
    afree(assets);
    afree(sheets);
    for (int i = 0; i < acount(project->files); i++) {
        ui_source *source = parse_file(&project->files[i]);
        if (source) {
            learn(project, source);
        }
        ui_source_free(source);
    }
}

void ui_source_project_free(ui_source_project *project)
{
    for (int i = 0; i < acount(project->files); i++) {
        free(project->files[i].path);
        free(project->files[i].name);
    }
    afree(project->files);
    list_free(project->widgets);
    list_free(project->classes);
    list_free(project->variables);
    list_free(project->properties);
    list_free(project->save_keys);
    *project = (ui_source_project){ 0 };
}

/* ---------- Completion ---------- */

typedef struct request
{
    const char *value;
    int at; /* the offset completing, in the value */
    ui_source_completions *out;
} request;

static bool name_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

static bool identifier_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* Starts the word ending at the offset, of the characters `accept` takes. */
static int word_start(const request *r, bool (*accept)(char))
{
    int start = r->at;
    while (start > 0 && accept(r->value[start - 1])) {
        start--;
    }
    return start;
}

static void offer(request *r, ui_source_completion_kind kind, const char *label, int start)
{
    size_t typed = (size_t)(r->at - start);
    if (strncasecmp(label, r->value + start, typed) != 0) {
        return;
    }
    for (int i = 0; i < acount(r->out->items); i++) {
        if (!strcmp(r->out->items[i].label, label)) {
            return;
        }
    }
    apush(r->out->items, ((ui_source_completion){ kind, xstrdup(label) }));
}

static void offer_words(request *r, ui_source_completion_kind kind, const char *const *words, int start)
{
    for (int i = 0; words[i]; i++) {
        offer(r, kind, words[i], start);
    }
}

static void offer_list(request *r, ui_source_completion_kind kind, char **list, int start)
{
    for (int i = 0; i < acount(list); i++) {
        offer(r, kind, list[i], start);
    }
}

/* The completion's replaced range, as file offsets later. */
static void settle(request *r, int start)
{
    r->out->start = start;
    r->out->end = r->at;
}

/* Completes a data binding: the key after save. */
static void complete_binding(request *r, const ui_source_project *project)
{
    int start = word_start(r, identifier_char);
    if (start >= 5 && !strncmp(r->value + start - 5, "save.", 5) &&
        (start == 5 || !identifier_char(r->value[start - 6]))) {
        offer_list(r, UI_COMPLETE_SAVE_KEY, project->save_keys, start);
    }
    settle(r, start);
}

/* Where RCSS at `at` in [from, at) stands: in a selector, a declaration's
 * property or value, or a var(). */
typedef enum
{
    CSS_SELECTOR,
    CSS_PROPERTY,
    CSS_VALUE,
    CSS_VARIABLE,
    CSS_NONE,
} css_place;

typedef struct css_scan
{
    int depth;      /* braces open */
    int segment;    /* after the last {, } or ; */
    int parens[16]; /* where each open paren is */
    int paren_count;
    bool comment, line_comment;
    char quote;
} css_scan;

static void scan_css_char(css_scan *s, const char *v, int i)
{
    char c = v[i];
    if (s->comment || s->line_comment) {
        s->comment &= !(c == '/' && i && v[i - 1] == '*');
        s->line_comment &= c != '\n';
        return;
    }
    if (s->quote) {
        s->quote = c == s->quote ? 0 : s->quote;
        return;
    }
    if (c == '/' && (v[i + 1] == '*' || v[i + 1] == '/')) {
        s->comment = v[i + 1] == '*';
        s->line_comment = v[i + 1] == '/';
        return;
    }
    s->quote = c == '"' || c == '\'' ? c : 0;
    if (c == '(' && s->paren_count < 16) {
        s->parens[s->paren_count++] = i;
    } else if (c == ')' && s->paren_count) {
        s->paren_count--;
    } else if (c == '{' || c == '}' || c == ';') {
        s->depth += (c == '{') - (c == '}' && s->depth);
        s->segment = i + 1;
        s->paren_count = 0;
    }
}

static css_place css_place_at(const char *v, int from, int at, int depth)
{
    css_scan s = { .depth = depth, .segment = from };
    for (int i = from; i < at; i++) {
        scan_css_char(&s, v, i);
    }
    if (s.comment || s.line_comment || s.quote) {
        return CSS_NONE;
    }
    int open = s.paren_count ? s.parens[s.paren_count - 1] : -1;
    if (open >= 3 && !strncmp(v + open - 3, "var", 3)) {
        return CSS_VARIABLE;
    }
    if (!s.depth) {
        return CSS_SELECTOR;
    }
    const char *colon = memchr(v + s.segment, ':', (size_t)(at - s.segment));
    int first = s.segment;
    while (first < at && isspace((unsigned char)v[first])) {
        first++;
    }
    bool selector = first < at && strchr(".#&:[*>", v[first]);
    return selector ? CSS_SELECTOR : colon ? CSS_VALUE : CSS_PROPERTY;
}

static void complete_css(request *r, const ui_source_project *project, int from, int depth)
{
    int start = word_start(r, name_char);
    switch (css_place_at(r->value, from, r->at, depth)) {
        case CSS_VARIABLE:
            offer_list(r, UI_COMPLETE_VARIABLE, project->variables, start);
            break;
        case CSS_PROPERTY:
            offer_list(r, UI_COMPLETE_PROPERTY, project->properties, start);
            break;
        case CSS_SELECTOR:
            if (start > 0 && r->value[start - 1] == '.') {
                offer_list(r, UI_COMPLETE_CLASS, project->classes, start);
            } else if (start == 0 || !strchr("#:", r->value[start - 1])) {
                offer_words(r, UI_COMPLETE_TAG, TAGS, start);
            }
            break;
        case CSS_VALUE:
        case CSS_NONE: break;
    }
    settle(r, start);
}

/* Inside a tag of the markup: the open quote of the value being typed, if
 * any, and the tag's '<'. */
typedef struct tag_scan
{
    int open; /* the tag's '<', or -1 outside tags */
    int quote_at;
    char quote;
} tag_scan;

static tag_scan scan_markup(const char *v, int at)
{
    tag_scan s = { .open = -1, .quote_at = -1 };
    bool comment = false;
    for (int i = 0; i < at; i++) {
        if (comment) {
            comment = !(i >= 2 && !strncmp(v + i - 2, "-->", 3));
        } else if (s.open < 0) {
            comment = !strncmp(v + i, "<!--", 4);
            s.open = !comment && v[i] == '<' ? i : -1;
        } else if (s.quote) {
            s.quote = v[i] == s.quote ? 0 : s.quote;
        } else if (v[i] == '"' || v[i] == '\'') {
            s.quote = v[i];
            s.quote_at = i;
        } else if (v[i] == '>') {
            s.open = -1;
        }
    }
    s.open = comment ? -1 : s.open;
    return s;
}

/* The name of the attribute whose value opens at `quote`. */
static void attribute_before(const char *v, int quote, char *out, size_t size)
{
    int end = quote;
    while (end > 0 && (v[end - 1] == '=' || v[end - 1] == ' ')) {
        end--;
    }
    int start = end;
    while (start > 0 && (name_char(v[start - 1]) || v[start - 1] == ':')) {
        start--;
    }
    snprintf(out, size, "%.*s", end - start, v + start);
}

static void complete_value(request *r, const ui_source_project *project, const char *tag, int quote)
{
    char attribute[64];
    attribute_before(r->value, quote, attribute, sizeof attribute);
    int start = word_start(r, name_char);
    if (!strcmp(tag, "use") && !strcmp(attribute, "ui")) {
        offer_list(r, UI_COMPLETE_WIDGET, project->widgets, start);
    } else if (!strcmp(attribute, "class")) {
        offer_list(r, UI_COMPLETE_CLASS, project->classes, start);
    } else if (!strcmp(attribute, "style")) {
        complete_css(r, project, quote + 1, 1);
        return;
    } else if (ui_source_is_binding(attribute, (int)strlen(attribute))) {
        complete_binding(r, project);
        return;
    }
    settle(r, start);
}

static void complete_tag(request *r, const ui_source_project *project, const tag_scan *s)
{
    const char *v = r->value;
    int name = s->open + 1 + (v[s->open + 1] == '/');
    int name_end = name;
    while (name_end < r->at && name_char(v[name_end])) {
        name_end++;
    }
    if (name_end == r->at) {
        offer_words(r, UI_COMPLETE_TAG, TAGS, name);
        settle(r, name);
        return;
    }
    char tag[64];
    snprintf(tag, sizeof tag, "%.*s", name_end - name, v + name);
    if (s->quote) {
        complete_value(r, project, tag, s->quote_at);
        return;
    }
    int start = word_start(r, name_char);
    static const char CLASS_BINDING[] = "data-class-";
    if (r->at - start >= (int)sizeof CLASS_BINDING - 1 &&
        !strncmp(v + start, CLASS_BINDING, sizeof CLASS_BINDING - 1)) {
        start += (int)sizeof CLASS_BINDING - 1;
        offer_list(r, UI_COMPLETE_CLASS, project->classes, start);
    } else {
        offer_words(r, UI_COMPLETE_ATTRIBUTE, ATTRIBUTES, start);
        if (!strcmp(tag, "use")) {
            offer(r, UI_COMPLETE_ATTRIBUTE, "ui", start);
        }
    }
    settle(r, start);
}

/* Inside {{ }} in text. */
static bool in_binding_text(const char *v, int at)
{
    for (int i = at - 1; i > 0; i--) {
        if (v[i] == '}' && v[i - 1] == '}') {
            return false;
        }
        if (v[i] == '{' && v[i - 1] == '{') {
            return true;
        }
    }
    return false;
}

static void complete_markup(request *r, const ui_source_project *project)
{
    tag_scan s = scan_markup(r->value, r->at);
    if (s.open >= 0) {
        complete_tag(r, project, &s);
        return;
    }
    if (in_binding_text(r->value, r->at)) {
        complete_binding(r, project);
        return;
    }
    settle(r, r->at);
}

static int completion_order(const void *a, const void *b)
{
    const ui_source_completion *x = a, *y = b;
    return strcmp(x->label, y->label);
}

/* The block value offset of a file offset in `m`: the first value byte at
 * or after it. */
static int value_offset(const block_map *m, int offset)
{
    int low = 0, high = m->length;
    while (low < high) {
        int middle = (low + high) / 2;
        if (m->offsets[middle] < offset) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

void ui_source_complete(
    const ui_source *source,
    int offset,
    const ui_source_project *project,
    ui_source_completions *out
)
{
    *out = (ui_source_completions){ .start = offset, .end = offset };
    const ui_source_block *block = ui_source_block_at(source, offset);
    const block_map *m = block ? ui_source_map(source, block->language) : NULL;
    if (!m || m->style == SCALAR_OTHER) {
        return;
    }
    request r = { m->value, value_offset(m, offset), out };
    if (m->language == UI_SOURCE_RML) {
        complete_markup(&r, project);
    } else {
        complete_css(&r, project, 0, 0);
    }
    out->start = ui_source_file_offset(m, out->start);
    out->end = ui_source_file_offset(m, out->end);
    qsort(out->items, (size_t)acount(out->items), sizeof *out->items, completion_order);
}

void ui_source_completions_free(ui_source_completions *completions)
{
    for (int i = 0; i < acount(completions->items); i++) {
        free(completions->items[i].label);
    }
    afree(completions->items);
    *completions = (ui_source_completions){ 0 };
}

/* ---------- Definitions and references ---------- */

ui_source_symbol ui_source_symbol_at(const ui_source *source, int offset)
{
    occurrence *found = NULL;
    ui_source_occurrences(source, &found);
    ui_source_symbol symbol = { .kind = UI_SYMBOL_NONE };
    for (int i = 0; i < acount(found); i++) {
        const occurrence *o = &found[i];
        if (o->kind != UI_SYMBOL_SAVE_KEY && o->start <= offset && offset <= o->end) {
            symbol = (ui_source_symbol){ .kind = o->kind,
                                         .start = o->start,
                                         .end = o->end };
            snprintf(symbol.name, sizeof symbol.name, "%s", o->name);
            break;
        }
    }
    afree(found);
    return symbol;
}

static void add_location(
    ui_source_location **out,
    const ui_source *source,
    const char *path,
    int start,
    int end,
    bool definition
)
{
    ui_source_location l = { .path = xstrdup(path),
                             .start = start,
                             .end = end,
                             .definition = definition };
    ui_source_position(source, start, &l.line, &l.column);
    apush(*out, l);
}

static void file_references(
    const ui_source_file *file,
    const ui_source_symbol *symbol,
    bool definitions,
    ui_source_location **out
)
{
    ui_source *source = parse_file(file);
    if (!source) {
        return;
    }
    if (definitions && symbol->kind == UI_SYMBOL_WIDGET && file->name &&
        !strcmp(file->name, symbol->name)) {
        add_location(out, source, file->path, 0, 0, true);
    }
    occurrence *found = NULL;
    ui_source_occurrences(source, &found);
    for (int i = 0; i < acount(found); i++) {
        const occurrence *o = &found[i];
        if (o->kind == (int)symbol->kind && o->definition == definitions &&
            !strcmp(o->name, symbol->name)) {
            add_location(out, source, file->path, o->start, o->end, definitions);
        }
    }
    afree(found);
    ui_source_free(source);
}

void ui_source_references(
    const ui_source_project *project,
    const ui_source_symbol *symbol,
    ui_source_location **locations
)
{
    assert(symbol->kind != UI_SYMBOL_NONE);
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < acount(project->files); i++) {
            file_references(&project->files[i], symbol, pass == 0, locations);
        }
    }
}

void ui_source_locations_free(ui_source_location *locations)
{
    for (int i = 0; i < acount(locations); i++) {
        free(locations[i].path);
    }
    afree(locations);
}
