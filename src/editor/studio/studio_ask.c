/* Natural language commands from the command palette. An Ask row submits the
 * query to the editor command API; a loading toast follows the request
 * (Sonner's promise toast) and becomes its result. Undoable changes apply at
 * once and offer Undo while they are still their document's latest edit.
 * Irreversible changes and questions reopen the palette to confirm or answer. */
#include "editor/commands/command.h"
#include "editor/studio/studio_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* ---------- UI hooks ---------- */

static const studio_editor PANEL_EDITORS[] = {
    STUDIO_EDITOR_ASSETS,
    STUDIO_EDITOR_OUTPUT,
    STUDIO_EDITOR_PERFORMANCE,
};
static const char *const PANEL_NAMES[] = { "Assets", "Output", "Performance" };
_Static_assert(
    sizeof PANEL_EDITORS / sizeof PANEL_EDITORS[0] ==
        sizeof PANEL_NAMES / sizeof PANEL_NAMES[0],
    "each drawer panel has a name"
);

static bool panel_visible(void *user, int panel)
{
    const studio *s = user;
    return s->prefs.drawer_open &&
           s->prefs.editors[STUDIO_AREA_DRAWER] == PANEL_EDITORS[panel];
}

static void show_panel(void *user, int panel, bool visible)
{
    if (panel_visible(user, panel) != visible) {
        studio_toggle_drawer(user, PANEL_EDITORS[panel]);
    }
}

static void history(void *user, bool redo)
{
    studio_history(user, redo);
}

static void frame(void *user)
{
    studio_frame_selection(user);
}

void studio_commands_attach(studio *s)
{
    editor_command_hooks hooks = {
        .user = s,
        .panels = PANEL_NAMES,
        .panel_count = sizeof PANEL_NAMES / sizeof PANEL_NAMES[0],
        .panel_visible = panel_visible,
        .show_panel = show_panel,
        .history = history,
        .frame = frame,
    };
    editor_commands_set_hooks(s->scene, &hooks);
}

void studio_commands_detach(studio *s)
{
    editor_commands_set_hooks(s->scene, NULL);
}

/* ---------- Requests ---------- */

typedef struct
{
    char *json;
    yyjson_doc *doc;
    yyjson_val *root;
} ask_reply;

static ask_reply parse(char *json)
{
    ask_reply r = { .json = json, .doc = yyjson_read(json, strlen(json), 0) };
    r.root = yyjson_doc_get_root(r.doc);
    return r;
}

static void reply_free(ask_reply *r)
{
    yyjson_doc_free(r->doc);
    free(r->json);
}

static const char *text(yyjson_val *object, const char *key)
{
    const char *value = yyjson_get_str(yyjson_obj_get(object, key));
    return value ? value : "";
}

/* One API request about `task`, with an optional string or boolean. */
static ask_reply call(studio *s, const char *operation, const char *task, const char *key, const char *value)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "operation", operation);
    yyjson_mut_obj_add_str(doc, root, "task_id", task);
    if (key && value) {
        yyjson_mut_obj_add_strcpy(doc, root, key, value);
    } else if (key) {
        yyjson_mut_obj_add_bool(doc, root, key, true);
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    ask_reply r = parse(editor_commands_api(s->scene, json));
    free(json);
    return r;
}

/* The task's summary lines, joined. */
static void summary(yyjson_val *root, char *out, size_t size)
{
    out[0] = 0;
    size_t i, count;
    yyjson_val *line;
    yyjson_arr_foreach(yyjson_obj_get(root, "summary"), i, count, line)
    {
        size_t used = strlen(out);
        snprintf(out + used, size - used, "%s%s", used ? "; " : "", yyjson_get_str(line));
    }
}

static void failed(studio *s, yyjson_val *root)
{
    const char *status = text(root, "status");
    const char *title = !strcmp(status, "unsupported") ? "Unsupported"
                      : !strcmp(status, "unverified")  ? "Unverified"
                                                       : "Command failed";
    const char *message = text(root, "message");
    if (s->ask.task[0] && strcmp(text(root, "task_id"), s->ask.task)) {
        /* A rejected reply leaves the task waiting; the palette has let it go. */
        ask_reply rejected = call(s, "cancel", s->ask.task, NULL, NULL);
        reply_free(&rejected);
    }
    char record[512];
    snprintf(record, sizeof record, "%s: %s", title, message);
    studio_record(s, KIT_TOAST_ERROR, record);
    kit_toast(
        s->kit,
        s->ask.toast,
        &(kit_toast_desc){ .type = KIT_TOAST_ERROR,
                           .title = title,
                           .description = message,
                           .duration = -1 }
    );
}

/* A finished task; its loading toast becomes the result. */
static void finish(studio *s, yyjson_val *root)
{
    const char *status = text(root, "status");
    s->ask.phase = STUDIO_ASK_IDLE;
    if (!strcmp(status, "cancelled")) {
        kit_toast_dismiss(s->kit, s->ask.toast);
    } else if (strcmp(status, "applied")) {
        failed(s, root);
    } else {
        char description[512], record[600];
        summary(root, description, sizeof description);
        bool undoable = yyjson_get_bool(yyjson_obj_get(root, "undoable"));
        kit_toast_dismiss(s->kit, s->ask.undo_toast);
        s->ask.undo_toast = kit_toast(
            s->kit,
            s->ask.toast,
            &(kit_toast_desc){ .type = KIT_TOAST_SUCCESS,
                               .title = "Applied",
                               .description = description,
                               .action = undoable ? "Undo" : NULL,
                               .duration = 6 }
        );
        snprintf(s->ask.undo_task, sizeof s->ask.undo_task, "%s", s->ask.task);
        snprintf(record, sizeof record, "Applied: %s", description);
        studio_record(s, KIT_TOAST_SUCCESS, record);
    }
    s->ask.toast = 0;
}

/* Waits on a pending task, or shows or finishes a settled one. */
static void follow(studio *s, yyjson_val *root)
{
    const char *status = text(root, "status");
    if (!strcmp(status, "pending")) {
        s->ask.phase = STUDIO_ASK_WAITING;
        s->ask.toast = kit_toast(
            s->kit,
            s->ask.toast,
            &(kit_toast_desc){ .type = KIT_TOAST_LOADING,
                               .title = "Resolving",
                               .description = s->ask.query,
                               .action = "Cancel" }
        );
        return;
    }
    if (!strcmp(status, "needs_confirmation") ||
        !strcmp(status, "needs_clarification")) {
        bool confirm = !strcmp(status, "needs_confirmation");
        s->ask.phase = confirm ? STUDIO_ASK_CONFIRM : STUDIO_ASK_CLARIFY;
        kit_toast_dismiss(s->kit, s->ask.toast);
        s->ask.toast = 0;
        kit_command_open(s->kit, confirm ? s->ask.query : text(root, "message"));
        return;
    }
    finish(s, root);
}

static void cancel(studio *s)
{
    ask_reply r = call(s, "cancel", s->ask.task, NULL, NULL);
    kit_toast_dismiss(s->kit, s->ask.toast);
    s->ask.toast = 0;
    s->ask.phase = STUDIO_ASK_IDLE;
    reply_free(&r);
}

/* A new request replaces the one in flight. */
void studio_ask(studio *s, const char *query)
{
    if (s->ask.phase != STUDIO_ASK_IDLE) {
        cancel(s);
    }
    snprintf(s->ask.query, sizeof s->ask.query, "%s", query);
    ask_reply r = call(s, "submit", "", "command", query);
    snprintf(s->ask.task, sizeof s->ask.task, "%s", text(r.root, "task_id"));
    follow(s, r.root);
    reply_free(&r);
}

static void respond(studio *s, const char *key, const char *value)
{
    ask_reply r = call(s, "respond", s->ask.task, key, value);
    follow(s, r.root);
    reply_free(&r);
}

static void undo(studio *s)
{
    ask_reply r = call(s, "respond", s->ask.undo_task, "undo", NULL);
    if (!strcmp(text(r.root, "status"), "undone")) {
        studio_report(s, "Undo");
    } else {
        studio_toast(s, KIT_TOAST_ERROR, "Could not undo", text(r.root, "message"));
    }
    reply_free(&r);
}

static bool in_palette(const studio *s)
{
    return s->ask.phase == STUDIO_ASK_CONFIRM || s->ask.phase == STUDIO_ASK_CLARIFY;
}

void studio_ask_frame(studio *s)
{
    if (s->ask.undo_toast && kit_toast_action(s->kit, s->ask.undo_toast)) {
        s->ask.undo_toast = 0;
        undo(s);
    }
    /* A later edit of the same document retires the Undo. */
    if (s->ask.undo_toast && !editor_commands_undoable(s->scene, s->ask.undo_task)) {
        kit_toast_dismiss(s->kit, s->ask.undo_toast);
        s->ask.undo_toast = 0;
    }
    if (s->ask.phase == STUDIO_ASK_WAITING && kit_toast_action(s->kit, s->ask.toast)) {
        s->ask.toast = 0;
        cancel(s);
    }
    if (in_palette(s) && !kit_command_is_open(s->kit)) {
        cancel(s); /* Escape or a press outside the palette */
    }
    if (s->ask.phase != STUDIO_ASK_WAITING) {
        return;
    }
    ask_reply r = parse(xstrdup(editor_commands_result(s->scene)));
    if (strcmp(text(r.root, "task_id"), s->ask.task)) {
        cancel(s);
    } else if (strcmp(text(r.root, "status"), "pending")) {
        follow(s, r.root);
    }
    reply_free(&r);
}

/* ---------- Palette ---------- */

/* One Confirm row under what will happen; Enter confirms, Escape cancels. */
static void confirm(studio *s, yyjson_val *root)
{
    char changes[512];
    summary(root, changes, sizeof changes);
    const kit_command_item row = { "Confirm", changes, NULL,
                                   "↵",       NULL,    KIT_ICON_CHECK };
    if (kit_command_ranked(s->kit, &row, 1) >= 0) {
        respond(s, "approve", NULL);
    }
}

/* The listed choices, or the typed answer when the question lists none. */
static void clarify(studio *s, yyjson_val *root)
{
    const char *question = text(root, "message");
    char typed[256];
    snprintf(typed, sizeof typed, "%s", kit_command_query(s->kit));
    yyjson_val *choices = yyjson_obj_get(root, "choices");
    kit_command_item *rows = NULL;
    size_t i, count;
    yyjson_val *choice;
    yyjson_arr_foreach(choices, i, count, choice)
    {
        apush(rows, ((kit_command_item){ text(choice, "label"), "", NULL, NULL, NULL, KIT_ICON_TARGET }));
    }
    if (!acount(rows)) {
        apush(rows, ((kit_command_item){ typed[0] ? typed : question, "", NULL, NULL, NULL, KIT_ICON_TERMINAL }));
    }
    int chosen = kit_command_ranked(s->kit, rows, acount(rows));
    const char *answer = yyjson_arr_size(choices)
                           ? text(yyjson_arr_get(choices, (size_t)chosen), "id")
                           : typed;
    if (chosen >= 0 && answer[0]) {
        respond(s, "value", answer);
    } else if (chosen >= 0) {
        kit_command_open(s->kit, question); /* nothing typed yet */
    }
    afree(rows);
}

bool studio_ask_palette(studio *s)
{
    if (!in_palette(s)) {
        return false;
    }
    ask_reply r = parse(xstrdup(editor_commands_result(s->scene)));
    if (s->ask.phase == STUDIO_ASK_CONFIRM) {
        confirm(s, r.root);
    } else {
        clarify(s, r.root);
    }
    reply_free(&r);
    return true;
}
