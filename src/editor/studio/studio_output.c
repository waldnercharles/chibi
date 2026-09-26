/* Output: the behavior log, compiler problems, the playing scene's runtime
 * errors (Unreal's message log) and the operation history (Blender's Info
 * editor), as virtualized, highlighted rows. Logs follow new output while
 * scrolled to the bottom; problems and errors open their node. Problems
 * cover the open asset as it compiles live, or every graph asset of the
 * project as of its last check. */
#include "editor/studio/studio_internal.h"
#include "editor/ui/highlight.h"
#include "editor/workspace/editor_internal.h"
#include "graph/compiler/compile.h"
#include "graph/document/problems.h"
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
    OUTPUT_FIND,  /* Find References (studio_find.c) */
    OUTPUT_CALLS, /* Call Hierarchy (studio_calls.c) */
    OUTPUT_VIEWS,
};

enum
{
    PROBLEMS_ASSET,
    PROBLEMS_PROJECT,
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

/* "Asset › Graph › Node" for a problem, as far as the asset still holds it. */
static void problem_place(const editor *e, const catalog_problem *problem, char *out, size_t size)
{
    graph *g = behavior_asset_find_graph(problem->asset, problem->graph_id);
    node *n = g && problem->node_id ? graph_node_by_id(g, problem->node_id) : NULL;
    if (!g) {
        snprintf(out, size, "%s", problem->asset->name);
        return;
    }
    const char *name = g->name && g->name[0] ? g->name : "Event Graph";
    if (!n) {
        snprintf(out, size, "%s \xe2\x80\xba %s", problem->asset->name, name);
        return;
    }
    char title[128];
    ed_node_title_in(e, problem->asset, g, graph_inline_owner(g, n), title, sizeof title);
    snprintf(out, size, "%s \xe2\x80\xba %s \xe2\x80\xba %s", problem->asset->name, name, title);
}

/* Validates every graph asset of the project, with its unsaved edits. That
   compiles them all, so it runs only while the project's problems show and
   something was saved or reloaded since the last check, or on Refresh. */
static void check_project(studio *s)
{
    const editor *e = s->scene->graph_editor;
    catalog_problem *found = behavior_catalog_problems(e->asset_catalog, acount(e->asset_catalog));
    asetlen(s->project_problems, 0);
    for (int i = 0; i < acount(found); i++) {
        studio_problem p = { .at = { .graph_id = found[i].graph_id,
                                     .node_id = found[i].node_id },
                             .error = found[i].severity == CMPL_ERROR };
        snprintf(p.at.asset, sizeof p.at.asset, "%s", found[i].asset->name);
        snprintf(p.message, sizeof p.message, "%s", found[i].message);
        problem_place(e, &found[i], p.place, sizeof p.place);
        apush(s->project_problems, p);
    }
    afree(found);
    s->problems_checked = true;
}

static int problem_count(const studio *s)
{
    if (s->problems_scope == PROBLEMS_PROJECT) {
        return acount(s->project_problems);
    }
    const compile_report *report = problems(s);
    return report ? report->error_count + report->warning_count : 0;
}

/* The scope of the Problems view, and Refresh for the project's. */
static void problems_header(studio *s)
{
    kit *k = s->kit;
    static const char *const SCOPES[] = { "Asset", "Project" };
    kit_push(k, "scope", NULL, "output-scope");
    kit_toggle_group(k, "scope", &s->problems_scope, SCOPES, 2, 0);
    kit_pop(k);
    if (s->problems_scope != PROBLEMS_PROJECT) {
        return;
    }
    kit_button_desc refresh = { .icon = KIT_ICON_REFRESH, .tip = "Refresh" };
    if (kit_button(k, "refresh", &refresh)) {
        s->problems_checked = false;
    }
}

void studio_output_header(studio *s, int area)
{
    (void)area;
    kit *k = s->kit;
    if (s->problems_scope == PROBLEMS_PROJECT && !s->problems_checked &&
        s->scene->graph_editor) {
        check_project(s);
    }
    int count = problem_count(s);
    int errors = acount(s->runtime_errors);
    char problems_label[32], runtime_label[32];
    snprintf(problems_label, sizeof problems_label, count ? "Problems %d" : "Problems", count);
    snprintf(runtime_label, sizeof runtime_label, errors ? "Runtime %d" : "Runtime", errors);
    const char *views[OUTPUT_VIEWS] = { "Log",  problems_label, runtime_label,
                                        "Info", "Find",         "Calls" };
    kit_push(k, "views", NULL, "output-views");
    kit_toggle_group(k, "view", &s->output_view, views, OUTPUT_VIEWS, 0);
    kit_pop(k);
    if (s->output_view == OUTPUT_PROBLEMS) {
        problems_header(s);
    }
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
        case OUTPUT_CALLS: clearable = s->calls.root.title[0] != '\0'; break;
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
        case OUTPUT_CALLS: studio_calls_clear(s); break;
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

/* Rows open their asset and reveal the node, like the open asset's. */
static void project_view(studio *s, float row)
{
    kit *k = s->kit;
    int total = acount(s->project_problems);
    int *shown = NULL;
    for (int i = 0; i < total; i++) {
        const studio_problem *p = &s->project_problems[i];
        if (contains(p->message, s->output_filter) ||
            contains(p->place, s->output_filter)) {
            apush(shown, i);
        }
    }
    kit_list_desc list = { .count = acount(shown), .row_height = row, .reveal = -1 };
    int first, last;
    kit_list_begin(k, "project", "output-list", &list, &first, &last);
    for (int r = first; r <= last && r < acount(shown); r++) {
        const studio_problem *p = &s->project_problems[shown[r]];
        char key[24];
        snprintf(key, sizeof key, "q%d", shown[r]);
        located_row_begin(k, key, p->error ? KIT_TOAST_ERROR : KIT_TOAST_WARNING, p->place, p->message);
        kit_pop(k);
        if (kit_item_clicked(k)) {
            studio_reveal(s, &p->at);
        }
    }
    kit_list_end(k);
    if (!acount(shown)) {
        empty(k, total ? "No matches" : "No problems");
    }
    afree(shown);
}

static void problems_view(studio *s, float row)
{
    if (s->problems_scope == PROBLEMS_PROJECT) {
        project_view(s, row);
        return;
    }
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
        studio_node_ref at = studio_problem_at(s, d);
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
        case OUTPUT_CALLS: studio_calls_view(s, row); break;
        default: log_view(s, row); break;
    }
}

/* A maximized area gives way so the editor shows. */
void studio_show_editor(studio *s, studio_editor editor)
{
    if (s->maximized >= 0 && s->prefs.editors[s->maximized] != editor) {
        s->maximized = -1;
    }
    for (int area = 0; area < STUDIO_AREA_COUNT; area++) {
        bool shown = area != STUDIO_AREA_DRAWER || s->prefs.drawer_open;
        if (shown && s->prefs.editors[area] == editor) {
            return;
        }
    }
    studio_toggle_drawer(s, editor);
}

void studio_output_find(studio *s)
{
    s->output_view = OUTPUT_FIND;
    studio_show_editor(s, STUDIO_EDITOR_OUTPUT);
}

void studio_output_calls(studio *s)
{
    s->output_view = OUTPUT_CALLS;
    studio_show_editor(s, STUDIO_EDITOR_OUTPUT);
}
