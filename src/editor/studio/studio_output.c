/* Output: the behavior log, compiler problems, the playing scene's runtime
 * errors (Unreal's message log) and the operation history (Blender's Info
 * editor), as virtualized, highlighted rows. Logs follow new output while
 * scrolled to the bottom; problems and errors open their node. */
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
    OUTPUT_RUNTIME,
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
    int errors = acount(s->runtime_errors);
    char problems_label[32], runtime_label[32];
    snprintf(problems_label, sizeof problems_label, count ? "Problems %d" : "Problems", count);
    snprintf(runtime_label, sizeof runtime_label, errors ? "Runtime %d" : "Runtime", errors);
    const char *views[OUTPUT_VIEWS] = { "Log", problems_label, runtime_label, "Info", "Find" };
    kit_push(k, "views", NULL, "output-views");
    kit_toggle_group(k, "view", &s->output_view, views, OUTPUT_VIEWS, 0);
    kit_pop(k);
    kit_leaf(k, "spacer", NULL, "spacer");
    kit_push(k, "filter", NULL, "output-filter");
    kit_search(k, "search", s->output_filter, sizeof s->output_filter, "Filter");
    kit_pop(k);
    bool clearable = false;
    switch (s->output_view) {
        case OUTPUT_LOG:
            clearable = s->scene->graph_editor &&
                        editor_log_count(s->scene->graph_editor) > 0;
            break;
        case OUTPUT_RUNTIME: clearable = errors > 0; break;
        case OUTPUT_INFO: clearable = acount(s->records) > 0; break;
        case OUTPUT_FIND: clearable = s->references_title[0] != '\0'; break;
    }
    kit_button_desc clear = { .icon = KIT_ICON_TRASH,
                              .tip = "Clear",
                              .flags = clearable ? 0 : KIT_DISABLED };
    if (!kit_button(k, "clear", &clear)) {
        return;
    }
    switch (s->output_view) {
        case OUTPUT_LOG: editor_clear_log(s->scene->graph_editor); break;
        case OUTPUT_RUNTIME: asetlen(s->runtime_errors, 0); break;
        case OUTPUT_INFO: asetlen(s->records, 0); break;
        case OUTPUT_FIND: studio_find_clear(s); break;
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

/* Opens a row for something at a node: an icon, where, then the message. The
   caller may add to the row, then pops it. */
static void located_row_begin(kit *k, const char *key, kit_toast_type kind, const char *place, const char *message)
{
    char markup[OUTPUT_LINE * 2];
    kit_push(k, key, NULL, "output-row located");
    kit_attr(k, "data-kind", kind_name(kind));
    kit_icon(k, "icon", "kind", kind_icon(kind));
    kit_label(k, "at", "output-at", place);
    highlight_markup(HIGHLIGHT_LOG, message, markup, sizeof markup);
    kit_markup(k, "text", "output-line", markup);
}

static void problems_view(studio *s, float row)
{
    kit *k = s->kit;
    const compile_report *report = problems(s);
    int total = report ? acount(report->items) : 0;
    const char *behavior = s->scene->graph_document_id;
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
        studio_node_ref at = { .graph_id = d->graph_id, .node_id = d->node_id };
        snprintf(at.asset, sizeof at.asset, "%s", behavior ? behavior : "");
        char key[24], place[256];
        snprintf(key, sizeof key, "p%d", shown[r]);
        studio_node_place(s, &at, place, sizeof place);
        located_row_begin(
            k,
            key,
            d->severity == CMPL_ERROR     ? KIT_TOAST_ERROR
            : d->severity == CMPL_WARNING ? KIT_TOAST_WARNING
                                          : KIT_TOAST_INFO,
            place,
            d->message
        );
        kit_pop(k);
        if (kit_item_clicked(k) && behavior) {
            studio_reveal(s, &at);
        }
    }
    kit_list_end(k);
    if (!acount(shown)) {
        empty(k, total ? "No matches" : "No problems");
    }
    afree(shown);
}

/* Runs that failed while the scene played, one row per node and message. */
static void runtime_view(studio *s, float row)
{
    kit *k = s->kit;
    int total = acount(s->runtime_errors);
    int *shown = NULL;
    for (int i = 0; i < total; i++) {
        const studio_runtime_error *error = &s->runtime_errors[i];
        if (contains(error->message, s->output_filter) ||
            contains(error->behavior, s->output_filter) ||
            contains(error->node, s->output_filter)) {
            apush(shown, i);
        }
    }
    kit_list_desc list = { .count = acount(shown), .row_height = row, .reveal = -1 };
    int first, last;
    kit_list_begin(k, "runtime", "output-list", &list, &first, &last);
    for (int r = first; r <= last && r < acount(shown); r++) {
        const studio_runtime_error *error = &s->runtime_errors[shown[r]];
        char key[24], place[256];
        snprintf(key, sizeof key, "r%d", shown[r]);
        snprintf(
            place,
            sizeof place,
            error->node[0] ? "%s \xe2\x80\xba %s" : "%s",
            error->behavior,
            error->node
        );
        located_row_begin(k, key, KIT_TOAST_ERROR, place, error->message);
        if (error->count > 1) {
            char count[24];
            snprintf(count, sizeof count, "\xc3\x97%d", error->count);
            kit_label(k, "count", "badge output-count", count);
        }
        kit_pop(k);
        if (kit_item_clicked(k)) {
            studio_reveal(s, &error->at);
        }
    }
    kit_list_end(k);
    if (!acount(shown)) {
        empty(k, total ? "No matches" : "No runtime errors");
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
        case OUTPUT_RUNTIME: runtime_view(s, row); break;
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
