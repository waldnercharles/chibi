/* What a place in a ui asset names, as an editor's hover describes it: the
 * symbol, where the project defines it and how often it is used
 * (ui_source.h). Studio's Text Editor shows it in its hover card; native
 * inspection returns it with a symbol's references. */
#include "assets/ui_source.h"
#include "support/array.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Definitions a description lists before counting the rest. */
#define SHOWN_DEFINITIONS 3
/* Characters of a definition's line a description quotes. */
#define QUOTED_LINE 72

/* Appends formatted text to an array of chars kept NUL-terminated. */
static void append(char **out, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int size = vsnprintf(NULL, 0, format, args);
    va_end(args);
    int length = acount(*out) ? acount(*out) - 1 : 0;
    asetlen(*out, length + size + 1);
    va_start(args, format);
    vsnprintf(*out + length, (size_t)size + 1, format, args);
    va_end(args);
}

/* Owned text for the caller, from the array. */
static char *finish(char *out)
{
    char *text = xstrdup(out);
    afree(out);
    return text;
}

/* A project path from its ui folder on, as "ui/theme/roles.rcss". */
static const char *shown_path(const char *path)
{
    const char *ui = NULL;
    for (const char *p = strstr(path, "/ui/"); p; p = strstr(p + 1, "/ui/")) {
        ui = p + 1;
    }
    return ui ? ui : path;
}

/* Line `line` of the file, trimmed and cut to QUOTED_LINE characters. */
static void quoted_line(const char *path, int line, char *out, size_t size)
{
    out[0] = '\0';
    char *text = text_file_read(path);
    const char *at = text;
    for (int n = 1; at && n < line; n++) {
        at = strchr(at, '\n');
        at = at ? at + 1 : NULL;
    }
    if (at) {
        at += strspn(at, " \t");
        int length = (int)strcspn(at, "\r\n");
        bool cut = length > QUOTED_LINE;
        snprintf(out, size, "%.*s%s", cut ? QUOTED_LINE : length, at, cut ? "\xe2\x80\xa6" : "");
    }
    free(text);
}

static const char *kind_title(ui_source_symbol_kind kind)
{
    switch (kind) {
        case UI_SYMBOL_WIDGET: return "Widget";
        case UI_SYMBOL_VARIABLE: return "Custom property";
        case UI_SYMBOL_CLASS: return "Class";
        case UI_SYMBOL_SAVE_KEY: return "Save key";
        case UI_SYMBOL_NONE: break;
    }
    return "Symbol";
}

static const char *missing(ui_source_symbol_kind kind)
{
    switch (kind) {
        case UI_SYMBOL_WIDGET: return "No ui asset has this name";
        case UI_SYMBOL_VARIABLE: return "No stylesheet declares it";
        case UI_SYMBOL_CLASS: return "No rule selects it";
        case UI_SYMBOL_SAVE_KEY: return "Saved data holds it";
        case UI_SYMBOL_NONE: break;
    }
    return "";
}

static void definitions(char **out, const ui_source_symbol *symbol, const ui_source_location *found)
{
    int count = 0;
    for (int i = 0; i < acount(found); i++) {
        const ui_source_location *l = &found[i];
        if (!l->definition || count++ >= SHOWN_DEFINITIONS) {
            continue;
        }
        char line[QUOTED_LINE + 8];
        quoted_line(l->path, l->line, line, sizeof line);
        if (symbol->kind == UI_SYMBOL_WIDGET) {
            append(out, "\n%s", shown_path(l->path));
        } else {
            append(out, "\n%s:%d  %s", shown_path(l->path), l->line, line);
        }
    }
    if (count > SHOWN_DEFINITIONS) {
        append(out, "\nand %d more", count - SHOWN_DEFINITIONS);
    }
    if (!count) {
        append(out, "\n%s", missing(symbol->kind));
    }
}

char *ui_source_describe(const ui_source_project *project, const ui_source_symbol *symbol)
{
    ui_source_location *found = NULL;
    ui_source_references(project, symbol, &found);
    char *out = NULL;
    const char *dot = symbol->kind == UI_SYMBOL_CLASS ? "." : "";
    append(&out, "%s %s%s", kind_title(symbol->kind), dot, symbol->name);
    definitions(&out, symbol, found);
    int uses = 0;
    for (int i = 0; i < acount(found); i++) {
        uses += !found[i].definition;
    }
    append(&out, "\n%d %s", uses, uses == 1 ? "use" : "uses");
    ui_source_locations_free(found);
    return finish(out);
}

static const ui_source_span *span_at(const ui_source *source, int offset)
{
    for (int i = 0; i < acount(source->spans); i++) {
        const ui_source_span *span = &source->spans[i];
        if (span->start <= offset && offset < span->end) {
            return span;
        }
    }
    return NULL;
}

static bool listed(char **list, const char *name)
{
    for (int i = 0; i < acount(list); i++) {
        if (!strcmp(list[i], name)) {
            return true;
        }
    }
    return false;
}

static char *describe_span(const ui_source *source, const ui_source_span *span, const ui_source_project *project)
{
    char name[128];
    snprintf(name, sizeof name, "%.*s", span->end - span->start, source->text + span->start);
    char *out = NULL;
    if (span->kind == UI_SOURCE_PROPERTY && strncmp(name, "--", 2)) {
        append(&out, "Property %s", name);
        append(&out, "\n%s", listed(project->properties, name) ? "RCSS property" : "Not a property RmlUi knows");
        return finish(out);
    }
    if (span->kind == UI_SOURCE_BINDING) {
        append(&out, "Binding %s", name);
        append(&out, "\nA name the document's data model gives it");
        return finish(out);
    }
    return NULL;
}

char *ui_source_hover(const ui_source *source, int offset, const ui_source_project *project)
{
    ui_source_symbol symbol = ui_source_symbol_at(source, offset);
    if (symbol.kind != UI_SYMBOL_NONE && offset < symbol.end) {
        return ui_source_describe(project, &symbol);
    }
    const ui_source_span *span = span_at(source, offset);
    return span ? describe_span(source, span, project) : NULL;
}
