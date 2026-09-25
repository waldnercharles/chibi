/* Output: the behavior log, compiler problems and the operation history
 * (Blender's Info editor), as virtualized, highlighted rows. Logs follow new
 * output while scrolled to the bottom. */
#include "editor/studio/studio_internal.h"
#include "editor/ui/highlight.h"
#include "graph/compiler/compile.h"
#include "support/array.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum
{
    OUTPUT_LOG,
    OUTPUT_PROBLEMS,
    OUTPUT_INFO,
    OUTPUT_FIND, /* Find References (studio_find.c) */
    OUTPUT_VIEWS,
};

#define OUTPUT_LINE 1536

static bool contains(const char *text, const char *filter)
{
    if (!filter[0]) {
        return true;
    }
    for (const char *t = text; *t; t++) {
        const char *a = t, *b = filter;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) {
            return true;
        }
    }
    return false;
}

void studio_record(studio *s, kit_toast_type kind, const char *text)
{
    if (acount(s->records) >= 500) {
        memmove(
            s->records,
            s->records + 1,
            (size_t)(acount(s->records) - 1) * sizeof *s->records
        );
        asetlen(s->records, acount(s->records) - 1);
    }
    studio_record_entry entry = { .time = s->time, .kind = kind };
    snprintf(entry.text, sizeof entry.text, "%s", text);
    apush(s->records, entry);
}

static const compile_report *problems(const studio *s)
{
    if (!s->scene->graph_editor) {
        return NULL;
    }
    editor_query q;
    editor_query_state(s->scene->graph_editor, &q);
    return q.compile;
}

void studio_output_header(studio *s, int area)
{
    (void)area;
    kit *k = s->kit;
    const compile_report *report = problems(s);
    int count = report ? report->error_count + report->warning_count : 0;
    char problems_label[32];
    snprintf(problems_label, sizeof problems_label, count ? "Problems %d" : "Problems", count);
    const char *views[OUTPUT_VIEWS] = { "Log", problems_label, "Info", "Find" };
    kit_push(k, "views", NULL, "output-views");
    kit_toggle_group(k, "view", &s->output_view, views, OUTPUT_VIEWS, 0);
    kit_pop(k);
    kit_leaf(k, "spacer", NULL, "spacer");
    kit_push(k, "filter", NULL, "output-filter");
    kit_search(k, "search", s->output_filter, sizeof s->output_filter, "Filter");
    kit_pop(k);
    bool clearable = s->output_view == OUTPUT_LOG
                       ? s->scene->graph_editor &&
                             editor_log_count(s->scene->graph_editor) > 0
                   : s->output_view == OUTPUT_INFO ? acount(s->records) > 0
                   : s->output_view == OUTPUT_FIND ? s->references_title[0] != '\0'
                                                   : false;
    kit_button_desc clear = { .icon = KIT_ICON_TRASH,
                              .tip = "Clear",
                              .flags = clearable ? 0 : KIT_DISABLED };
    if (kit_button(k, "clear", &clear)) {
        if (s->output_view == OUTPUT_LOG) {
            editor_clear_log(s->scene->graph_editor);
        } else if (s->output_view == OUTPUT_FIND) {
            studio_find_clear(s);
        } else {
            asetlen(s->records, 0);
        }
    }
}

static void empty(kit *k, const char *text)
{
    kit_label(k, "empty", "output-empty", text);
}

static uint32_t kind_icon(kit_toast_type kind)
{
    switch (kind) {
        case KIT_TOAST_SUCCESS: return KIT_ICON_PASS;
        case KIT_TOAST_WARNING: return KIT_ICON_WARNING;
        case KIT_TOAST_ERROR: return KIT_ICON_ERROR;
        default: return KIT_ICON_INFO;
    }
}

static const char *kind_name(kit_toast_type kind)
{
    switch (kind) {
        case KIT_TOAST_SUCCESS: return "success";
        case KIT_TOAST_WARNING: return "warning";
        case KIT_TOAST_ERROR: return "error";
        default: return "info";
    }
}

/* Row markup: a muted lead, then the highlighted text. */
static void row_markup(char *out, size_t size, const char *lead, const char *name, const char *text)
{
    char body[OUTPUT_LINE];
    highlight_markup(HIGHLIGHT_LOG, text, body, sizeof body);
    snprintf(
        out,
        size,
        "<span class=\"tok-muted\">%s</span> %s%s%s%s",
        lead,
        name ? "<span class=\"tok-name\">" : "",
        name ? name : "",
        name ? "</span> " : "",
        body
    );
}

static void log_view(studio *s, float row)
{
    kit *k = s->kit;
    editor *e = s->scene->graph_editor;
    int total = e ? editor_log_count(e) : 0;
    int *shown = NULL;
    char origin[128];
    for (int i = 0; i < total; i++) {
        editor_log_line line = editor_log_at(e, i, origin, sizeof origin);
        if (contains(line.message, s->output_filter) ||
            contains(origin, s->output_filter)) {
            apush(shown, i);
        }
    }
    kit_list_desc list = { .count = acount(shown),
                           .row_height = row,
                           .follow = true,
                           .reveal = -1 };
    int first, last;
    kit_list_begin(k, "log", "output-list", &list, &first, &last);
    for (int r = first; r <= last && r < acount(shown); r++) {
        editor_log_line line = editor_log_at(e, shown[r], origin, sizeof origin);
        char lead[48], markup[OUTPUT_LINE * 2], key[24];
        snprintf(lead, sizeof lead, "%8.3f", line.time);
        row_markup(markup, sizeof markup, lead, origin, line.message);
        snprintf(key, sizeof key, "l%d", shown[r]);
        kit_markup(k, key, "output-line", markup);
    }
    kit_list_end(k);
    if (!acount(shown)) {
        empty(k, total ? "No matches" : "No output");
    }
    afree(shown);
}

static void problems_view(studio *s, float row)
{
    kit *k = s->kit;
    const compile_report *report = problems(s);
    int total = report ? acount(report->items) : 0;
    int *shown = NULL;
    for (int i = 0; i < total; i++) {
        if (contains(report->items[i].message, s->output_filter)) {
            apush(shown, i);
        }
    }
    kit_list_desc list = { .count = acount(shown), .row_height = row, .reveal = -1 };
    int first, last;
    kit_list_begin(k, "problems", "output-list", &list, &first, &last);
    for (int r = first; r <= last && r < acount(shown); r++) {
        const compile_diagnostic *d = &report->items[shown[r]];
        char key[24], markup[OUTPUT_LINE * 2];
        snprintf(key, sizeof key, "p%d", shown[r]);
        kit_push(k, key, NULL, "output-row");
        kit_attr(
            k,
            "data-kind",
            d->severity == CMPL_ERROR     ? "error"
            : d->severity == CMPL_WARNING ? "warning"
                                          : "info"
        );
        kit_icon(
            k,
            "icon",
            "kind",
            d->severity == CMPL_ERROR     ? KIT_ICON_ERROR
            : d->severity == CMPL_WARNING ? KIT_ICON_WARNING
                                          : KIT_ICON_INFO
        );
        row_markup(markup, sizeof markup, cmpl_rule_name(d->rule), NULL, d->message);
        kit_markup(k, "text", "output-line", markup);
        kit_pop(k);
    }
    kit_list_end(k);
    if (!acount(shown)) {
        empty(k, total ? "No matches" : "No problems");
    }
    afree(shown);
}

static void info_view(studio *s, float row)
{
    kit *k = s->kit;
    int *shown = NULL;
    for (int i = 0; i < acount(s->records); i++) {
        if (contains(s->records[i].text, s->output_filter)) {
            apush(shown, i);
        }
    }
    kit_list_desc list = { .count = acount(shown),
                           .row_height = row,
                           .follow = true,
                           .reveal = -1 };
    int first, last;
    kit_list_begin(k, "info", "output-list", &list, &first, &last);
    for (int r = first; r <= last && r < acount(shown); r++) {
        const studio_record_entry *entry = &s->records[shown[r]];
        char key[24], lead[24], markup[OUTPUT_LINE * 2];
        int seconds = (int)entry->time;
        snprintf(lead, sizeof lead, "%02d:%02d", seconds / 60 % 100, seconds % 60);
        snprintf(key, sizeof key, "i%d", shown[r]);
        kit_push(k, key, NULL, "output-row");
        kit_attr(k, "data-kind", kind_name(entry->kind));
        kit_icon(k, "icon", "kind", kind_icon(entry->kind));
        row_markup(markup, sizeof markup, lead, NULL, entry->text);
        kit_markup(k, "text", "output-line", markup);
        kit_pop(k);
    }
    kit_list_end(k);
    if (!acount(shown)) {
        empty(k, acount(s->records) ? "No matches" : "No operations yet");
    }
    afree(shown);
}

void studio_output_body(studio *s, int area)
{
    (void)area;
    float row = s->prefs.comfortable ? 24 : 20;
    switch (s->output_view) {
        case OUTPUT_PROBLEMS: problems_view(s, row); break;
        case OUTPUT_INFO: info_view(s, row); break;
        case OUTPUT_FIND: studio_find_view(s, row); break;
        default: log_view(s, row); break;
    }
}

/* A maximized area gives way so the results show. */
void studio_output_find(studio *s)
{
    s->output_view = OUTPUT_FIND;
    if (s->maximized >= 0 && s->prefs.editors[s->maximized] != STUDIO_EDITOR_OUTPUT) {
        s->maximized = -1;
    }
    for (int area = 0; area < STUDIO_AREA_COUNT; area++) {
        bool shown = area != STUDIO_AREA_DRAWER || s->prefs.drawer_open;
        if (shown && s->prefs.editors[area] == STUDIO_EDITOR_OUTPUT) {
            return;
        }
    }
    studio_toggle_drawer(s, STUDIO_EDITOR_OUTPUT);
}
