/* The Text Editor area (Blender's): the source of a UI document, its RML and
 * RCSS, or a stylesheet's RCSS. It follows the active UI document and keeps
 * the last one while a document of another kind is active; focusing its text
 * makes it active again, so Save and Undo follow it. Edits apply across Studio
 * as they are typed (the code_apply_live preference), or when saved. */
#include "editor/studio/studio_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Frames before a revealed line is laid out and can scroll into view. */
#define REVEAL_FRAMES 2

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

void studio_code_close(studio *s, int index)
{
    scene_document_tab *tab = &s->scene->tabs[index];
    assert(tab->kind == APP_EDITOR_DOCUMENT_UI);
    if (!ui_buffer_dirty(tab->ui)) {
        scene_tabs_close(s->scene, APP_EDITOR_DOCUMENT_UI, tab->path);
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
    snprintf(s->code.close, sizeof s->code.close, "%s", tab->path);
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
        if (ui_tab(s, s->code.close)) {
            scene_tabs_close(s->scene, APP_EDITOR_DOCUMENT_UI, s->code.close);
        }
    }
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
    scene_document_tab *active = active_ui(s);
    if (active && strcmp(active->path, s->code.path)) {
        snprintf(s->code.path, sizeof s->code.path, "%s", active->path);
        studio_show_editor(s, STUDIO_EDITOR_CODE);
    }
    toast_actions(s);
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
    s->code.reveal_wait = REVEAL_FRAMES;
    studio_show_editor(s, STUDIO_EDITOR_CODE);
    return true;
}

bool studio_open_ui_source(studio *s, const char *path, int line)
{
    return studio_open_ui_part(s, path, UI_PART_RML, line);
}

/* ---------- Header ---------- */

static void file_label(studio *s, const scene_document_tab *tab)
{
    kit *k = s->kit;
    kit_push(k, "file", NULL, "code-file");
    kit_icon(k, "icon", NULL, tab->ui->stylesheet ? KIT_ICON_FILE : KIT_ICON_LAYOUT);
    kit_label(k, "name", "name", content_basename(tab->path));
    kit_tip(k, tab->path, NULL, NULL);
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

void studio_code_header(studio *s, int)
{
    kit *k = s->kit;
    scene_document_tab *tab = shown(s);
    if (!tab) {
        kit_label(k, "title", "dim", "No UI document open");
        return;
    }
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
    problem_chip(k, b);
    int mode = s->prefs.code_apply_live ? 0 : 1;
    kit_push(k, "apply", NULL, "code-apply");
    if (kit_toggle_group(k, "mode", &mode, APPLY_LABELS, 2, 0)) {
        s->prefs.code_apply_live = mode == 0;
    }
    kit_tip(k, "Apply edits", NULL, "Live applies each valid edit as you type; On Save waits for the file");
    kit_pop(k);
}

/* ---------- Body ---------- */

/* The one edit that turns the editor's text into `text`: the bytes between
 * their common start and end. */
static void follow_text(kit_code_editor *e, const char *text)
{
    size_t length, target = strlen(text);
    const char *shown = kit_code_text(e, &length);
    if (length == target && !memcmp(shown, text, length)) {
        return;
    }
    size_t start = 0, end = 0;
    while (start < length && start < target && shown[start] == text[start]) {
        start++;
    }
    while (end < length - start && end < target - start &&
           shown[length - 1 - end] == text[target - 1 - end]) {
        end++;
    }
    char *insert = xmalloc(target - start - end + 1);
    memcpy(insert, text + start, target - start - end);
    insert[target - start - end] = '\0';
    kit_code_apply(e, &(text_edit){ (int)start, (int)(length - end), insert }, 1);
    free(insert);
}

/* The code editor over the shown part. The buffer keeps the text and its
 * history: typing reaches it as typed text, and a change made elsewhere (the
 * designer, the MCP, Studio's Undo, a reload) comes back to the editor as an
 * edit, so the cursor keeps its place. Another document or part starts over. */
static kit_code_editor *part_editor(studio *s, const scene_document_tab *tab)
{
    studio_code *c = &s->code;
    const ui_buffer *b = tab->ui;
    const char *text = ui_buffer_text(b, b->part);
    if (!c->editor) {
        c->editor = kit_code_editor_create(text);
    }
    if (strcmp(c->editor_path, tab->path) || c->editor_part != b->part) {
        snprintf(c->editor_path, sizeof c->editor_path, "%s", tab->path);
        c->editor_part = b->part;
        kit_code_set_text(c->editor, text);
    }
    return c->editor;
}

void studio_code_free(studio *s)
{
    kit_code_editor_destroy(s->code.editor);
    s->code.editor = NULL;
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

/* Puts the cursor on the line studio_open_ui_part asked for, once laid out,
 * and focuses the editor. Call right after it. */
static void reveal(studio *s, kit_code_editor *e, const ui_buffer *b)
{
    if (!s->code.reveal || s->code.reveal_wait-- > 0) {
        return;
    }
    kit_code_set_cursor(e, line_offset(ui_buffer_text(b, b->part), s->code.reveal));
    kit_focus(s->kit);
    s->code.reveal = 0;
}

/* A chord the editor leaves to Studio runs the command it names, as the
 * shortcuts would if typing had not turned them off. */
static void run_chord(studio *s, const char *chord)
{
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
    scene_document_tab *tab = shown(s);
    if (!tab) {
        empty_state(k);
        return;
    }
    ui_buffer *b = tab->ui;
    kit_push(k, "code", NULL, "code-editor");
    kit_code_editor *e = part_editor(s, tab);
    kit_code_desc desc = {
        .vim = s->prefs.code_vim,
        .font_size = s->prefs.code_font_size,
        .mac = s->mac,
    };
    kit_code_result r = kit_code(k, "text", e, &desc);
    reveal(s, e, b);
    /* Its text in focus makes the document active, so Save and Undo reach it. */
    bool typed = r.events & KIT_CODE_CHANGED;
    if ((r.focused || typed) && tab != active_ui(s)) {
        scene_tabs_activate(s->scene, (int)(tab - s->scene->tabs));
    }
    if (typed) {
        ui_buffer_type(b, b->part, kit_code_text(e, NULL), s->time);
    }
    follow_text(e, ui_buffer_text(b, b->part));
    if (r.events & KIT_CODE_SAVED) {
        studio_save(s);
    }
    if (r.events & KIT_CODE_CHORD) {
        run_chord(s, r.chord);
    }
    if (b->error[0]) {
        kit_push(k, "status", NULL, "code-status");
        kit_icon(k, "icon", NULL, KIT_ICON_ERROR);
        kit_label(k, "message", "message", b->error);
        kit_pop(k);
    }
    kit_pop(k);
}
