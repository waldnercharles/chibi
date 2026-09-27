/* The Text Editor area (Blender's): the source of a UI document, its RML and
 * RCSS, or a stylesheet's RCSS. It follows the active UI document and keeps
 * the last one while a document of another kind is active; focusing its text
 * makes it active again, so Save and Undo follow it. Edits apply across Studio
 * as they are typed (the code_apply_live preference), or when saved.
 *
 * Code intelligence (ui_code.h), after VS Code's editor: syntax errors as the
 * part parses and RmlUi's warnings once its text rests, as gutter markers and
 * a count in the header that steps through them; completion as you type and
 * on Control+Space; a card describing what the pointer rests on; Go to
 * Definition (F12 or a primary click) and Find References (Shift+F12) into
 * Output's Find view.
 *
 * One history: the document's buffer (ui_buffer.h) keeps it and the code
 * editors keep none. Typing reaches the buffer as typed text, an accepted
 * completion as a step of its own, and the undo keys (Z with the primary
 * modifier, Vim's u and Control+R) step the buffer as Studio's Undo does.
 * Whatever changes the buffer comes back to the editor as an edit, so its
 * cursor keeps its place; after an undo it lands on the change. Each open
 * document part keeps its own code editor, with its cursor, scroll and Vim
 * state. */
#include "editor/studio/studio_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Frames before a revealed line is laid out and can scroll into view. */
#define REVEAL_FRAMES 2
/* Seconds text rests before RmlUi checks it. */
#define CHECK_DELAY 0.3
/* Seconds a scan of the project serves before completion, hover and
   navigation scan again. */
#define PROJECT_STALE 2.0
/* Characters that start completion when typed, as '<' starts a tag. */
#define COMPLETION_TRIGGERS "<.\"("

static const char *const PART_LABELS[UI_PART_COUNT] = { "RML", "RCSS" };
static const char *const APPLY_LABELS[] = { "Live", "On Save" };

static scene_document_tab *active_ui(studio *s)
{
    app_scene_authoring *scene = s->scene;
    scene_document_tab *tab = scene->active_tab >= 0 &&
                                      scene->active_tab < acount(scene->tabs)
                                ? &scene->tabs[scene->active_tab]
                                : NULL;
    return tab && tab->kind == APP_EDITOR_DOCUMENT_UI ? tab : NULL;
}

static scene_document_tab *ui_tab(studio *s, const char *path)
{
    for (int i = 0; i < acount(s->scene->tabs); i++) {
        scene_document_tab *tab = &s->scene->tabs[i];
        if (tab->kind == APP_EDITOR_DOCUMENT_UI && !strcmp(tab->path, path)) {
            return tab;
        }
    }
    return NULL;
}

/* The document the Text Editor shows: the active one, else the last shown
 * while it is open, else the last opened. */
static scene_document_tab *shown(studio *s)
{
    scene_document_tab *tab = active_ui(s);
    tab = tab ? tab : ui_tab(s, s->code.path);
    for (int i = acount(s->scene->tabs) - 1; !tab && i >= 0; i--) {
        if (s->scene->tabs[i].kind == APP_EDITOR_DOCUMENT_UI) {
            tab = &s->scene->tabs[i];
        }
    }
    return tab;
}

ui_buffer *studio_ui_buffer(studio *s)
{
    scene_document_tab *tab = active_ui(s);
    return tab ? tab->ui : NULL;
}

/* ---------- Saving and closing ---------- */

static void save(studio *s, scene_document_tab *tab)
{
    if (save_ui_document(s->scene, tab) != 0) {
        studio_error(s, "Could not save", app_scene_authoring_message(s->scene));
        return;
    }
    char title[160];
    snprintf(title, sizeof title, "Saved %s", tab->label);
    kit_toast(s->kit, 0, &(kit_toast_desc){ .type = KIT_TOAST_SUCCESS, .title = title });
    studio_record(s, KIT_TOAST_SUCCESS, title);
}

/* A file another program changed since it loaded is saved over only when
 * asked, as editors ask before overwriting. */
void studio_code_save(studio *s)
{
    scene_document_tab *tab = active_ui(s);
    assert(tab);
    if (ui_buffer_on_disk(tab->ui)) {
        save(s, tab);
        return;
    }
    kit_toast_dismiss(s->kit, s->code.overwrite_toast);
    snprintf(s->code.overwrite, sizeof s->code.overwrite, "%s", tab->path);
    s->code.overwrite_toast = kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){ .type = KIT_TOAST_WARNING,
                           .title = "Changed on disk",
                           .description = tab->label,
                           .action = "Save Anyway",
                           .duration = -1 }
    );
}

/* A UI document's tab closes with its text; another discards what its
   document has unsaved, as reverting it would. */
static void close_tab(studio *s, int index)
{
    scene_document_tab *tab = &s->scene->tabs[index];
    if (tab->kind == APP_EDITOR_DOCUMENT_UI) {
        scene_tabs_close(s->scene, APP_EDITOR_DOCUMENT_UI, tab->path);
        return;
    }
    scene_tabs_discard(s->scene, index);
}

void studio_close_tab(studio *s, int index)
{
    scene_document_tab *tab = &s->scene->tabs[index];
    if (!scene_tabs_dirty(s->scene, tab)) {
        close_tab(s, index);
        return;
    }
    char title[160];
    snprintf(title, sizeof title, "Close %s without saving?", tab->label);
    kit_toast_desc ask = {
        .type = KIT_TOAST_WARNING,
        .title = title,
        .action = "Close",
        .duration = 6,
    };
    kit_toast_dismiss(s->kit, s->code.close_toast);
    snprintf(s->code.close, sizeof s->code.close, "%s", tab->key);
    s->code.close_toast = kit_toast(s->kit, 0, &ask);
}

static void toast_actions(studio *s)
{
    kit *k = s->kit;
    if (s->code.overwrite_toast && kit_toast_action(k, s->code.overwrite_toast)) {
        s->code.overwrite_toast = 0;
        scene_document_tab *tab = ui_tab(s, s->code.overwrite);
        if (tab) {
            save(s, tab);
        }
    }
    if (s->code.close_toast && kit_toast_action(k, s->code.close_toast)) {
        s->code.close_toast = 0;
        for (int i = 0; i < acount(s->scene->tabs); i++) {
            if (!strcmp(s->scene->tabs[i].key, s->code.close)) {
                close_tab(s, i);
                break;
            }
        }
    }
}

static int line_offset(const char *text, int line)
{
    int at = 0;
    for (int n = 1; n < line && text[at]; n++) {
        at += (int)strcspn(text + at, "\n");
        at += text[at] == '\n';
    }
    return at;
}

/* ---------- Code editors ---------- */

/* The code editor of the tab's shown part, made on first show. */
static studio_code_editor *part_editor(studio *s, const scene_document_tab *tab)
{
    studio_code *c = &s->code;
    const ui_buffer *b = tab->ui;
    for (int i = 0; i < acount(c->editors); i++) {
        if (!strcmp(c->editors[i].path, tab->path) && c->editors[i].part == b->part) {
            return &c->editors[i];
        }
    }
    studio_code_editor made = {
        .part = b->part,
        .editor = kit_code_editor_create(ui_buffer_text(b, b->part)),
        .synced = xstrdup(ui_buffer_text(b, b->part)),
    };
    snprintf(made.path, sizeof made.path, "%s", tab->path);
    apush(c->editors, made);
    return &alast(c->editors);
}

/* Drops the editors of documents that closed. */
static void prune_editors(studio *s)
{
    studio_code *c = &s->code;
    for (int i = acount(c->editors) - 1; i >= 0; i--) {
        if (ui_tab(s, c->editors[i].path)) {
            continue;
        }
        if (c->editor == c->editors[i].editor) {
            c->editor = NULL;
        }
        kit_code_editor_destroy(c->editors[i].editor);
        free(c->editors[i].synced);
        adel(c->editors, i);
    }
}

void studio_code_free(studio *s)
{
    studio_code *c = &s->code;
    for (int i = 0; i < acount(c->editors); i++) {
        kit_code_editor_destroy(c->editors[i].editor);
        free(c->editors[i].synced);
    }
    afree(c->editors);
    c->editor = NULL;
    ui_source_free(c->source);
    c->source = NULL;
    afree(c->spans);
    for (int p = 0; p < UI_PART_COUNT; p++) {
        ui_code_markers_free(c->markers[p]);
    }
    ui_check_messages_free(c->checked);
    rml_backend_destroy(c->checker);
    ui_source_project_free(&c->project);
    ui_code_completions_free(&c->completions);
    free(c->hover);
    afree(c->problems.rows);
    afree(s->ui_project_problems);
}

/* ---------- Checking ---------- */

/* RmlUi checks the shown document once its text has rested. */
static void check_shown(studio *s)
{
    studio_code *c = &s->code;
    scene_document_tab *tab = shown(s);
    if (!tab) {
        return;
    }
    ui_buffer *b = tab->ui;
    if (!strcmp(c->checked_path, tab->path) && c->checked_revision == b->revision) {
        return;
    }
    if (b->revision != c->check_seen) {
        c->check_seen = b->revision;
        c->check_due = s->time + CHECK_DELAY;
    }
    if (s->time < c->check_due) {
        return;
    }
    if (!c->checker) {
        c->checker = rml_backend_create(1280, 720, 1);
    }
    ui_check_messages_free(c->checked);
    c->checked = NULL;
    c->check_failure[0] = '\0';
    ui_code_check(
        b,
        rml_backend_context(c->checker),
        &c->checked,
        c->check_failure,
        sizeof c->check_failure
    );
    snprintf(c->checked_path, sizeof c->checked_path, "%s", tab->path);
    c->checked_revision = b->revision;
    c->markers_stale = true;
}

/* The Text Editor follows the active UI document as the other areas do: a
   document opens it, and a drawer it opened closes when no UI document is
   active, leaving the graph its room. */
static void follow_active(studio *s, const scene_document_tab *active)
{
    studio_code *c = &s->code;
    if (!active) {
        if (c->opened_drawer &&
            s->prefs.editors[STUDIO_AREA_DRAWER] == STUDIO_EDITOR_CODE) {
            s->prefs.drawer_open = false;
        }
        c->following = c->opened_drawer = false;
        return;
    }
    if (c->following && !strcmp(active->path, c->path)) {
        return;
    }
    snprintf(c->path, sizeof c->path, "%s", active->path);
    c->opened_drawer |= !studio_editor_shown(s, STUDIO_EDITOR_CODE);
    c->following = true;
    studio_show_editor(s, STUDIO_EDITOR_CODE);
}

void studio_code_frame(studio *s)
{
    bool applied = false;
    for (int i = 0; i < acount(s->scene->tabs); i++) {
        scene_document_tab *tab = &s->scene->tabs[i];
        if (tab->kind == APP_EDITOR_DOCUMENT_UI) {
            applied |= ui_buffer_update(tab->ui, s->time, s->prefs.code_apply_live);
        }
    }
    /* References follow the text that stands in for the files. */
    if (applied) {
        asset_index_invalidate(s->scene);
    }
    follow_active(s, active_ui(s));
    prune_editors(s);
    check_shown(s);
    toast_actions(s);
    /* Mouse buttons 4 and 5 over the Text Editor step Back and Forward. */
    int over = s->hover_area;
    if (s->nav_button && over >= 0 && s->prefs.editors[over] == STUDIO_EDITOR_CODE) {
        studio_nav_step(s, s->nav_button);
        s->nav_button = 0;
    }
}

/* ---------- Opening ---------- */

bool studio_open_ui_part(studio *s, const char *path, ui_part part, int line)
{
    finish_transform_edit(s->scene, false);
    if (scene_tabs_open_ui(s->scene, path) != 0) {
        studio_error(s, content_basename(path), app_scene_authoring_message(s->scene));
        return false;
    }
    ui_buffer *b = studio_ui_buffer(s);
    b->part = ui_buffer_text(b, part) ? part : UI_PART_RCSS;
    s->code.reveal = line;
    s->code.reveal_offset = -1;
    s->code.reveal_wait = REVEAL_FRAMES;
    studio_show_editor(s, STUDIO_EDITOR_CODE);
    return true;
}

bool studio_open_ui_source(studio *s, const char *path, int line)
{
    size_t length = strlen(path), extension = strlen(UI_STYLE_EXTENSION);
    bool styles = length > extension &&
                  !strcmp(path + length - extension, UI_STYLE_EXTENSION);
    return studio_open_ui_part(s, path, styles ? UI_PART_RCSS : UI_PART_RML, line);
}

bool studio_code_open_place(studio *s, const ui_code_place *place)
{
    if (!studio_open_ui_part(s, place->path, place->part, place->line)) {
        return false;
    }
    s->code.reveal_offset = place->offset;
    return true;
}

/* ---------- Problems (workstream Q) ---------- */

/* Each part's markers, as the Text Editor marks them, as Output's rows: a
   row opens the start of what its marker spans, or its line. */
static void add_problems(
    const ui_buffer *b,
    const ui_check_message *messages,
    const char *failure,
    studio_ui_problem **out
)
{
    for (int p = 0; p < UI_PART_COUNT; p++) {
        const char *text = ui_buffer_text(b, (ui_part)p);
        if (!text) {
            continue;
        }
        ui_source *source = ui_source_parse(text, ui_code_language((ui_part)p));
        kit_code_marker *markers = NULL;
        ui_code_markers(source, (ui_part)p, messages, failure, &markers);
        for (int i = 0; i < acount(markers); i++) {
            const kit_code_marker *m = &markers[i];
            studio_ui_problem row = { .at = { .part = (ui_part)p,
                                              .offset = m->end > m->start ? m->start : -1,
                                              .line = m->line },
                                      .error = m->severity == KIT_CODE_ERROR };
            snprintf(row.at.path, sizeof row.at.path, "%s", b->path);
            snprintf(row.message, sizeof row.message, "%s", m->message);
            apush(*out, row);
        }
        ui_code_markers_free(markers);
        ui_source_free(source);
    }
}

const studio_ui_problem *studio_code_problems(studio *s)
{
    studio_code *c = &s->code;
    scene_document_tab *tab = shown(s);
    if (!tab) {
        asetlen(c->problems.rows, 0);
        c->problems.path[0] = '\0';
        return c->problems.rows;
    }
    bool checked = !strcmp(c->checked_path, tab->path);
    unsigned check = checked ? c->checked_revision : 0;
    if (!strcmp(c->problems.path, tab->path) &&
        c->problems.revision == tab->ui->revision && c->problems.checked == check) {
        return c->problems.rows;
    }
    asetlen(c->problems.rows, 0);
    add_problems(
        tab->ui,
        checked ? c->checked : NULL,
        checked ? c->check_failure : NULL,
        &c->problems.rows
    );
    snprintf(c->problems.path, sizeof c->problems.path, "%s", tab->path);
    c->problems.revision = tab->ui->revision;
    c->problems.checked = check;
    return c->problems.rows;
}

/* One file of the project: an open document as it stands, else as read. */
static void file_problems(studio *s, const char *path, studio_ui_problem **out)
{
    scene_document_tab *tab = ui_tab(s, path);
    char error[256] = "";
    ui_buffer *b = tab ? tab->ui : ui_buffer_open(path, error, sizeof error);
    if (!b) {
        studio_ui_problem row = { .at = { .offset = -1, .line = 1 }, .error = true };
        snprintf(row.at.path, sizeof row.at.path, "%s", path);
        snprintf(row.message, sizeof row.message, "%s", error);
        apush(*out, row);
        return;
    }
    ui_check_message *messages = NULL;
    ui_code_check(b, rml_backend_context(s->code.checker), &messages, error, sizeof error);
    add_problems(b, messages, error, out);
    ui_check_messages_free(messages);
    if (!tab) {
        ui_buffer_free(b);
    }
}

static bool scanned(const ui_source_project *project, const char *path)
{
    for (int i = 0; i < acount(project->files); i++) {
        if (!strcmp(project->files[i].path, path)) {
            return true;
        }
    }
    return false;
}

/* The project's ui assets and stylesheets, then the open documents that
   lie elsewhere. */
void studio_code_project_problems(studio *s, studio_ui_problem **out)
{
    if (!s->code.checker) {
        s->code.checker = rml_backend_create(1280, 720, 1);
    }
    char root[PATH_MAX];
    ui_source_project project = { 0 };
    if (realpath(s->scene->content_root, root)) {
        ui_source_project_scan(&project, root);
    }
    for (int i = 0; i < acount(project.files); i++) {
        /* A ui asset's .rcss is a part of the document its .rml opens. */
        char *document = ui_buffer_document(project.files[i].path);
        if (!strcmp(document, project.files[i].path)) {
            file_problems(s, document, out);
        }
        free(document);
    }
    for (int i = 0; i < acount(s->scene->tabs); i++) {
        const scene_document_tab *tab = &s->scene->tabs[i];
        if (tab->kind == APP_EDITOR_DOCUMENT_UI && !scanned(&project, tab->path)) {
            file_problems(s, tab->path, out);
        }
    }
    ui_source_project_free(&project);
}

/* ---------- Header ---------- */

/* The shown part's file. */
static void file_label(studio *s, const scene_document_tab *tab)
{
    kit *k = s->kit;
    const char *file = tab->ui->files[tab->ui->part];
    kit_push(k, "file", NULL, "code-file");
    kit_icon(k, "icon", NULL, tab->ui->stylesheet ? KIT_ICON_FILE : KIT_ICON_LAYOUT);
    kit_label(k, "name", "name", content_basename(file));
    kit_tip(k, file, NULL, NULL);
    if (ui_buffer_dirty(tab->ui)) {
        kit_leaf(k, "dirty", NULL, "dirty-dot");
        kit_tip(k, "Unsaved", studio_chord(s, "Mod+S"), NULL);
    }
    kit_pop(k);
}

static void problem_chip(kit *k, const ui_buffer *b)
{
    if (!b->error[0]) {
        return;
    }
    kit_push(k, "problem", NULL, "code-problem");
    kit_icon(k, "icon", NULL, KIT_ICON_ERROR);
    kit_label(k, "label", NULL, "Invalid");
    kit_tip(k, "Keeps the last valid version", NULL, b->error);
    kit_pop(k);
}

/* The first problem of a part after `line`, or 0. */
static int problem_after(const kit_code_marker *markers, int line)
{
    for (int i = 0; i < acount(markers); i++) {
        if (markers[i].line > line) {
            return markers[i].line;
        }
    }
    return 0;
}

/* The next problem after the cursor's line, then the other part's first,
   wrapping to the first. */
static void next_problem(studio *s, ui_buffer *b)
{
    studio_code *c = &s->code;
    kit_code_editor *e = c->editor;
    ui_part other = b->part == UI_PART_RML ? UI_PART_RCSS : UI_PART_RML;
    const char *text = kit_code_text(e, NULL);
    int cursor = kit_code_cursor(e), line = 1;
    for (int i = 0; i < cursor && text[i]; i++) {
        line += text[i] == '\n';
    }
    int next = problem_after(c->markers[b->part], line);
    if (!next && acount(c->markers[other])) {
        b->part = other;
        next = c->markers[other][0].line;
    }
    s->code.reveal = next ? next : problem_after(c->markers[b->part], 0);
    s->code.reveal_offset = -1;
    s->code.reveal_wait = next && b->part == other ? REVEAL_FRAMES : 0;
}

/* The document's problems: a count that steps to the next. */
static void problems_button(studio *s, ui_buffer *b)
{
    const studio_code *c = &s->code;
    int errors = 0, count = 0;
    for (int p = 0; p < UI_PART_COUNT; p++) {
        for (int i = 0; i < acount(c->markers[p]); i++) {
            errors += c->markers[p][i].severity == KIT_CODE_ERROR;
            count++;
        }
    }
    if (!count || !c->editor) {
        return;
    }
    char label[16], tip[64];
    snprintf(label, sizeof label, "%d", count);
    snprintf(
        tip,
        sizeof tip,
        "%d %s, %d %s \xc2\xb7 Next problem",
        errors,
        errors == 1 ? "error" : "errors",
        count - errors,
        count - errors == 1 ? "warning" : "warnings"
    );
    kit_button_desc button = { .label = label,
                               .icon = errors ? KIT_ICON_ERROR : KIT_ICON_WARNING,
                               .size = KIT_SM,
                               .cls = errors ? "code-problems errors" : "code-problems",
                               .morph = true,
                               .tip = tip };
    if (kit_button(s->kit, "problems", &button)) {
        next_problem(s, b);
    }
}

/* Back and Forward through the places Go to Definition left and reached,
   and the graphs visited (studio_nav_step). */
static void history_buttons(studio *s)
{
    kit *k = s->kit;
    kit_push(k, "history", NULL, "code-history");
    kit_button_desc back = { .icon = KIT_ICON_BACK,
                             .size = KIT_SM,
                             .tip = "Back",
                             .shortcut = studio_chord(s, "Mod+["),
                             .flags = studio_nav_can_step(s, -1) ? 0 : KIT_DISABLED };
    if (kit_button(k, "back", &back)) {
        studio_nav_step(s, -1);
    }
    kit_button_desc forward = { .icon = KIT_ICON_FORWARD,
                                .size = KIT_SM,
                                .tip = "Forward",
                                .shortcut = studio_chord(s, "Mod+]"),
                                .flags = studio_nav_can_step(s, 1) ? 0 : KIT_DISABLED };
    if (kit_button(k, "forward", &forward)) {
        studio_nav_step(s, 1);
    }
    kit_pop(k);
}

static void open_find(studio_code *c);

void studio_code_header(studio *s, int)
{
    kit *k = s->kit;
    scene_document_tab *tab = shown(s);
    if (!tab) {
        kit_label(k, "title", "dim", "No UI document open");
        return;
    }
    history_buttons(s);
    ui_buffer *b = tab->ui;
    if (!b->stylesheet) {
        int part = b->part;
        kit_push(k, "parts", NULL, "code-parts");
        if (kit_toggle_group(k, "part", &part, PART_LABELS, UI_PART_COUNT, 0)) {
            b->part = (ui_part)part;
        }
        kit_pop(k);
    }
    file_label(s, tab);
    kit_leaf(k, "spacer", NULL, "spacer");
    kit_button_desc find = { .icon = KIT_ICON_SEARCH,
                             .tip = "Find",
                             .shortcut = studio_chord(s, "Mod+F"),
                             .flags = s->code.find.open ? KIT_ON : 0 };
    if (kit_button(k, "find", &find)) {
        open_find(&s->code);
    }
    const char *mode = s->code.editor ? kit_code_mode(s->code.editor) : NULL;
    if (mode) {
        kit_label(k, "mode", "code-mode", mode);
        kit_tip(k, "Vim mode", NULL, NULL);
    }
    problems_button(s, b);
    problem_chip(k, b);
    int apply = s->prefs.code_apply_live ? 0 : 1;
    kit_push(k, "apply", NULL, "code-apply");
    if (kit_toggle_group(k, "mode", &apply, APPLY_LABELS, 2, 0)) {
        s->prefs.code_apply_live = apply == 0;
    }
    kit_tip(k, "Apply edits", NULL, "Live applies each valid edit as you type; On Save waits for the file");
    kit_pop(k);
}

/* The editor shows its part as the buffer holds it, the text they then
   share; after an undo or redo from the editor, its cursor lands on the
   change. */
static void follow_text(studio_code *c, studio_code_editor *shown, const char *text)
{
    if (ui_code_follow(shown->editor, text, c->replayed)) {
        c->replayed = false;
    }
    if (strcmp(shown->synced, text)) {
        free(shown->synced);
        shown->synced = xstrdup(text);
    }
}

/* ---------- Language ---------- */

/* The scan of the project around `path`, made again once stale; `keep`
   serves the last scan of the same project however old, as completion does
   between keystrokes. */
static const ui_source_project *project(studio *s, const char *path, bool keep)
{
    studio_code *c = &s->code;
    char root[1024];
    if (!ui_code_content_root(path, root, sizeof root)) {
        root[0] = '\0';
    }
    bool fresh = !strcmp(root, c->project_root) &&
                 (keep || s->time - c->project_time < PROJECT_STALE);
    if (!fresh) {
        ui_source_project_free(&c->project);
        if (root[0]) {
            ui_code_scan(&c->project, root);
        }
        snprintf(c->project_root, sizeof c->project_root, "%s", root);
        c->project_time = s->time;
    }
    return &c->project;
}

/* Parses the editor's text as its part's language when either changed, and
   highlights it with the parse's spans; true when it parsed. */
static bool parse_part(studio_code *c, kit_code_editor *e, ui_part part)
{
    ui_source_language language = ui_code_language(part);
    const char *text = kit_code_text(e, NULL);
    if (c->source && c->source->language == language && !strcmp(c->source->text, text)) {
        return false;
    }
    ui_source_free(c->source);
    c->source = ui_source_parse(text, language);
    asetlen(c->spans, 0);
    for (int i = 0; i < acount(c->source->spans); i++) {
        const ui_source_span *span = &c->source->spans[i];
        const char *cls = ui_source_kind_class(span->kind);
        if (cls) {
            apush(c->spans, ((kit_code_span){ span->start, span->end, cls }));
        }
    }
    return true;
}

/* Each part's problems: its syntax errors and, for the document RmlUi last
   checked, RmlUi's warnings about the part. The shown part is the editor's
   text, parsed; the other, the buffer's. */
static void mark_problems(studio_code *c, const scene_document_tab *tab, ui_part shown)
{
    bool checked = !strcmp(c->checked_path, tab->path);
    for (int p = 0; p < UI_PART_COUNT; p++) {
        const char *text = ui_buffer_text(tab->ui, (ui_part)p);
        ui_source *other = p != (int)shown && text
                             ? ui_source_parse(text, ui_code_language((ui_part)p))
                             : NULL;
        ui_code_markers_free(c->markers[p]);
        c->markers[p] = NULL;
        if (p == (int)shown || other) {
            ui_code_markers(
                p == (int)shown ? c->source : other,
                (ui_part)p,
                checked ? c->checked : NULL,
                checked ? c->check_failure : NULL,
                &c->markers[p]
            );
        }
        ui_source_free(other);
    }
    c->markers_stale = false;
}

/* What the pointer rests on, described once per place. */
static void describe_hover(studio *s, const ui_buffer *b, kit_code_editor *e)
{
    studio_code *c = &s->code;
    int at = kit_code_hover(e);
    if (at == c->hover_at && c->source == c->hover_source) {
        return;
    }
    free(c->hover);
    c->hover = at >= 0 ? ui_code_hover(c->source, at, project(s, b->path, false))
                       : NULL;
    c->hover_at = at;
    c->hover_source = c->source;
}

/* Parse, problems and hover for the part the editor shows. */
static void understand(studio *s, const scene_document_tab *tab, kit_code_editor *e, ui_part part)
{
    studio_code *c = &s->code;
    if (parse_part(c, e, part) || c->markers_stale) {
        mark_problems(c, tab, part);
    }
    describe_hover(s, tab->ui, e);
}

/* The completions at the cursor; the list closes where there are none, or
   where nothing was typed that asked for them. */
static void complete(studio *s, const ui_buffer *b, kit_code_editor *e, ui_part part, bool asked)
{
    studio_code *c = &s->code;
    parse_part(c, e, part);
    ui_code_completions_free(&c->completions);
    int cursor = kit_code_cursor(e);
    ui_code_complete(c->source, cursor, project(s, b->path, !asked), &c->completions);
    int typed = cursor - c->completions.start, count = acount(c->completions.items);
    bool done = count == 1 && (int)strlen(c->completions.items[0].label) == typed &&
                !strncmp(
                    c->source->text + c->completions.start,
                    c->completions.items[0].label,
                    (size_t)typed
                );
    c->completing = count > 0 && (asked || typed > 0) && !done;
}

/* A typed character that starts completion: a word's first, or a trigger. */
static bool starts_completion(const char *text, ui_code_change d)
{
    if (d.end != d.start || d.inserted != 1) {
        return false;
    }
    char typed = text[d.start];
    return isalnum((unsigned char)typed) || typed == '-' ||
           strchr(COMPLETION_TRIGGERS, typed);
}

/* ---------- Navigation ---------- */

/* A jump records where it left and where it landed, for Back and Forward. */
static void go_to_definition(studio *s, const ui_buffer *b, kit_code_editor *e)
{
    studio_code *c = &s->code;
    int at = kit_code_cursor(e);
    ui_code_place place;
    if (ui_code_definition(b, c->source, at, project(s, b->path, false), &place)) {
        ui_code_place from = { .part = b->part, .offset = at, .line = 1 };
        snprintf(from.path, sizeof from.path, "%s", b->path);
        for (int i = 0; i < at; i++) {
            from.line += c->source->text[i] == '\n';
        }
        if (studio_code_open_place(s, &place)) {
            studio_nav_jump(s, &from, &place);
        }
        return;
    }
    bool named = ui_source_symbol_at(c->source, at).kind != UI_SYMBOL_NONE;
    studio_report(s, named ? "No other definition" : "No symbol to go to here");
}

static const char *symbol_title(const ui_source_symbol *symbol, char *out, size_t size)
{
    static const char *const KINDS[] = {
        [UI_SYMBOL_NONE] = "",          [UI_SYMBOL_WIDGET] = "Widget ",
        [UI_SYMBOL_VARIABLE] = "",      [UI_SYMBOL_CLASS] = "Class .",
        [UI_SYMBOL_SAVE_KEY] = "save.",
    };
    snprintf(out, size, "%s%s", KINDS[symbol->kind], symbol->name);
    return out;
}

/* A Find row for a place: its file and line. */
static studio_reference place_row(const ui_code_place *place)
{
    studio_reference row = { .kind = REFERENCE_ASSET_UI,
                             .use = place->definition ? "Define" : "Use",
                             .ui = *place };
    char *file = ui_buffer_part_file(place->path, place->part);
    snprintf(row.asset, sizeof row.asset, "%s", content_basename(file));
    snprintf(row.place, sizeof row.place, "line %d", place->line);
    free(file);
    return row;
}

static void find_references(studio *s, const ui_buffer *b, kit_code_editor *e)
{
    studio_code *c = &s->code;
    ui_code_place *places = NULL;
    ui_source_symbol symbol = ui_code_places(
        b,
        c->source,
        kit_code_cursor(e),
        project(s, b->path, false),
        &places
    );
    if (symbol.kind == UI_SYMBOL_NONE) {
        studio_report(s, "No symbol to find here");
        return;
    }
    asetlen(s->references, 0);
    for (int i = 0; i < acount(places); i++) {
        apush(s->references, place_row(&places[i]));
    }
    afree(places);
    symbol_title(&symbol, s->references_title, sizeof s->references_title);
    studio_output_find(s);
}

/* ---------- Body ---------- */

/* Puts the cursor where studio_open_ui_part or a place asked, once laid
   out, and focuses the editor. Call right after kit_code. */
static void reveal(studio *s, kit_code_editor *e, const ui_buffer *b)
{
    if (!s->code.reveal || s->code.reveal_wait-- > 0) {
        return;
    }
    const char *text = ui_buffer_text(b, b->part);
    int at = s->code.reveal_offset >= 0 ? s->code.reveal_offset
                                        : line_offset(text, s->code.reveal);
    kit_code_set_cursor(e, math_min(at, (int)strlen(text)));
    kit_focus(s->kit);
    s->code.reveal = 0;
    s->code.reveal_offset = -1;
}

/* ---------- Find (VS Code's find widget) ---------- */

/* Where the next match of `query` starts at or after `from`, wrapping past
   the end, or -1 without one. Case is ignored, as VS Code's default. */
static int next_match(const char *text, const char *query, int from)
{
    int length = (int)strlen(text);
    size_t size = strlen(query);
    for (int i = 0; i < length; i++) {
        int at = (from + i) % length;
        if (!strncasecmp(text + at, query, size)) {
            return at;
        }
    }
    return -1;
}

/* Where the last match before `from` starts, wrapping past the start. */
static int previous_match(const char *text, const char *query, int from)
{
    int length = (int)strlen(text);
    size_t size = strlen(query);
    for (int i = 1; i <= length; i++) {
        int at = ((from - i) % length + length) % length;
        if (!strncasecmp(text + at, query, size)) {
            return at;
        }
    }
    return -1;
}

/* Counts the matches, and places the shown one among them. */
static void count_matches(studio_code *c, const char *text)
{
    size_t size = strlen(c->find.query);
    c->find.count = 0;
    c->find.index = -1;
    for (const char *at = text; size && *at; at++) {
        if (!strncasecmp(at, c->find.query, size)) {
            c->find.index = at - text == c->find.at ? c->find.count : c->find.index;
            c->find.count++;
        }
    }
}

static void show_match(studio_code *c, kit_code_editor *e, int at)
{
    c->find.at = at;
    if (at >= 0) {
        kit_code_select(e, at, at + (int)strlen(c->find.query));
    }
}

static void step_find(studio_code *c, kit_code_editor *e, const char *text, int direction)
{
    if (!c->find.query[0]) {
        return;
    }
    int from = c->find.at >= 0 ? c->find.at : kit_code_cursor(e);
    show_match(
        c,
        e,
        direction > 0 ? next_match(text, c->find.query, from + (c->find.at >= 0))
                      : previous_match(text, c->find.query, from)
    );
}

static void open_find(studio_code *c)
{
    c->find.open = c->find.focus = true;
    c->find.at = -1;
}

static void close_find(studio_code *c)
{
    c->find.open = false;
    c->find.refocus = true;
}

/* Enter and Down show the next match, Shift+Enter and Up the previous one,
   and Escape closes the widget, returning to the text. */
static void find_keys(studio *s, kit_code_editor *e, const char *text)
{
    studio_code *c = &s->code;
    for (int i = 0; i < acount(s->keys); i++) {
        SDL_Keycode key = s->keys[i].key;
        bool shift = s->keys[i].mod & SDL_KMOD_SHIFT;
        if (key == SDLK_ESCAPE) {
            close_find(c);
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            step_find(c, e, text, shift ? -1 : 1);
        } else if (key == SDLK_DOWN || key == SDLK_UP) {
            step_find(c, e, text, key == SDLK_DOWN ? 1 : -1);
        }
    }
}

/* The widget over the text's top right. A new query shows the first match
   from the shown one or the cursor, so typing narrows in place. */
static void find_widget(studio *s, kit_code_editor *e, const char *text)
{
    studio_code *c = &s->code;
    kit *k = s->kit;
    if (strcmp(c->find.seen, c->find.query)) {
        snprintf(c->find.seen, sizeof c->find.seen, "%s", c->find.query);
        int from = c->find.at >= 0 ? c->find.at : kit_code_cursor(e);
        show_match(c, e, c->find.query[0] ? next_match(text, c->find.query, from) : -1);
    }
    count_matches(c, text);
    kit_push(k, "find", NULL, "find-widget");
    kit_push(k, "bar", NULL, "find-widget-bar");
    kit_search(k, "query", c->find.query, sizeof c->find.query, "Find");
    /* Escape clears and blurs the field in the frame it arrives, so the
       field's focus a frame ago counts too. */
    bool typing = kit_item_active(k) || c->find.typing;
    c->find.typing = kit_item_active(k);
    if (c->find.focus) {
        kit_focus(k);
        c->find.focus = false;
    }
    char label[32] = "";
    if (c->find.query[0] && !c->find.count) {
        snprintf(label, sizeof label, "No results");
    } else if (c->find.index >= 0) {
        snprintf(label, sizeof label, "%d/%d", c->find.index + 1, c->find.count);
    } else if (c->find.count) {
        snprintf(label, sizeof label, "%d", c->find.count);
    }
    kit_label(k, "count", "find-widget-count", label);
    unsigned none = c->find.count ? 0 : KIT_DISABLED;
    kit_button_desc previous = { .icon = KIT_ICON_UP,
                                 .tip = "Previous",
                                 .shortcut = "\xe2\x87\xa7\xe2\x86\xb5",
                                 .flags = none };
    if (kit_button(k, "previous", &previous)) {
        step_find(c, e, text, -1);
    }
    kit_button_desc next = { .icon = KIT_ICON_DOWN,
                             .tip = "Next",
                             .shortcut = "\xe2\x86\xb5",
                             .flags = none };
    if (kit_button(k, "next", &next)) {
        step_find(c, e, text, 1);
    }
    kit_button_desc close = { .icon = KIT_ICON_CLOSE, .tip = "Close", .shortcut = "Esc" };
    if (kit_button(k, "close", &close)) {
        close_find(c);
    }
    kit_pop(k);
    kit_pop(k);
    if (typing && c->find.open) {
        find_keys(s, e, text);
    }
}

/* A chord the editor leaves to Studio runs the command it names, as the
   shortcuts would if typing had not turned them off; Mod+[ and Mod+] step
   Back and Forward, as in JetBrains' editors, and Mod+F finds. */
static void run_chord(studio *s, const char *chord)
{
    if (!strcmp(chord, "Mod+F")) {
        open_find(&s->code);
        return;
    }
    if (!strcmp(chord, "Mod+[") || !strcmp(chord, "Mod+]")) {
        studio_nav_step(s, chord[4] == ']' ? 1 : -1);
        return;
    }
    const char *shown = studio_chord(s, chord);
    for (int c = 0; c < STUDIO_COMMAND_COUNT; c++) {
        studio_command_info info = studio_command_describe(s, (studio_command)c);
        if (info.shortcut && !strcmp(info.shortcut, shown)) {
            studio_command_run(s, (studio_command)c);
            return;
        }
    }
    /* Studio's shortcuts search with Mod+P as well as Mod+K. */
    if (!strcmp(chord, "Mod+P")) {
        studio_command_run(s, STUDIO_COMMAND_SEARCH);
    }
}

/* The editor's text into the buffer, the undo keys through it, and the
   buffer back to the editor: one history. Typing composes with what
   changed the buffer since they last shared its text, as a designer edit
   in the same frame. Returns what typing changed. */
static ui_code_change take_edits(studio *s, ui_buffer *b, studio_code_editor *shown, ui_part part, unsigned events)
{
    studio_code *c = &s->code;
    c->replayed = false;
    bool completed = events & KIT_CODE_COMPLETED;
    ui_code_change typed = { 0 };
    if (events & (KIT_CODE_CHANGED | KIT_CODE_COMPLETED)) {
        typed = ui_code_record(b, part, shown->editor, shown->synced, s->time, completed);
    }
    bool redo = events & KIT_CODE_REDO;
    if ((events & (KIT_CODE_UNDO | KIT_CODE_REDO)) && ui_code_replay(b, redo)) {
        c->replayed = true;
        studio_report(s, redo ? "Redo" : "Undo");
    }
    follow_text(c, shown, ui_buffer_text(b, part));
    return typed;
}

/* Completion starts where typing or Control+Space asks, follows the text
   and the cursor while it runs, and ends with an accepted item, Escape or
   an undo. */
static void follow_completion(
    studio *s,
    const ui_buffer *b,
    kit_code_editor *e,
    ui_part part,
    unsigned events,
    ui_code_change typed
)
{
    studio_code *c = &s->code;
    bool typing = (events & KIT_CODE_CHANGED) && !(events & KIT_CODE_COMPLETED);
    bool asked = (events & KIT_CODE_SUGGEST) ||
                 (typing && starts_completion(kit_code_text(e, NULL), typed));
    if (events &
        (KIT_CODE_COMPLETED | KIT_CODE_DISMISSED | KIT_CODE_UNDO | KIT_CODE_REDO)) {
        c->completing = false;
    } else if (asked || (c->completing && (events & (KIT_CODE_CHANGED | KIT_CODE_CURSOR)))) {
        complete(s, b, e, part, asked);
    }
}

static void respond(
    studio *s,
    scene_document_tab *tab,
    studio_code_editor *shown,
    ui_part part,
    const kit_code_result *r
)
{
    ui_buffer *b = tab->ui;
    kit_code_editor *e = shown->editor;
    unsigned events = r->events;
    /* Its text in focus makes the document active, so Save and Undo reach it. */
    if ((r->focused || (events & KIT_CODE_CHANGED)) && tab != active_ui(s)) {
        scene_tabs_activate(s->scene, (int)(tab - s->scene->tabs));
    }
    ui_code_change typed = take_edits(s, b, shown, part, events);
    follow_completion(s, b, e, part, events, typed);
    if (events & KIT_CODE_SAVED) {
        studio_save(s);
    }
    if (events & KIT_CODE_CHORD) {
        run_chord(s, r->chord);
    }
    if (events & KIT_CODE_DEFINITION) {
        go_to_definition(s, b, e);
    }
    if (events & KIT_CODE_REFERENCES) {
        find_references(s, b, e);
    }
}

static void empty_state(kit *k)
{
    kit_push(k, "empty", NULL, "pending-editor");
    kit_icon(k, "icon", NULL, KIT_ICON_EXPRESSION);
    kit_label(k, "name", NULL, "Open a UI asset or stylesheet from Assets");
    kit_pop(k);
}

void studio_code_body(studio *s, int)
{
    kit *k = s->kit;
    studio_code *c = &s->code;
    scene_document_tab *tab = shown(s);
    if (!tab) {
        c->editor = NULL;
        empty_state(k);
        return;
    }
    ui_buffer *b = tab->ui;
    kit_push(k, "code", NULL, "code-editor");
    ui_part part = b->part;
    studio_code_editor *shown_editor = part_editor(s, tab);
    kit_code_editor *e = shown_editor->editor;
    /* An editor coming into view catches up with its part; the one in view
       may hold typing that kit_code has yet to report. */
    if (e != c->editor) {
        follow_text(c, shown_editor, ui_buffer_text(b, part));
        c->editor = e;
        snprintf(c->editor_path, sizeof c->editor_path, "%s", tab->path);
        c->editor_part = part;
    }
    understand(s, tab, e, part);
    kit_code_desc desc = {
        .vim = s->prefs.code_vim,
        .font_size = s->prefs.code_font_size,
        .mac = s->mac,
        .spans = c->spans,
        .span_count = acount(c->spans),
        .markers = c->markers[part],
        .marker_count = acount(c->markers[part]),
        .completions = c->completing ? c->completions.items : NULL,
        .completion_count = c->completing ? acount(c->completions.items) : 0,
        .completion_start = c->completions.start,
        .history = true,
        .no_wrap = !s->prefs.code_wrap,
        .hover = c->hover,
    };
    kit_code_result r = kit_code(k, "text", e, &desc);
    if (c->find.refocus) {
        kit_focus(k);
        c->find.refocus = false;
    }
    reveal(s, e, b);
    if (c->find.open) {
        find_widget(s, e, ui_buffer_text(b, part));
    }
    respond(s, tab, shown_editor, part, &r);
    if (b->error[0]) {
        kit_push(k, "status", NULL, "code-status");
        kit_icon(k, "icon", NULL, KIT_ICON_ERROR);
        kit_label(k, "message", "message", b->error);
        kit_pop(k);
    }
    kit_pop(k);
}
