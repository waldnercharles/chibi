#ifndef CHIBI_RENDER_UI_H
#define CHIBI_RENDER_UI_H

#include "ecs/ecs.h"
#include "platform/cute.h"
#include "runtime/behaviors/entities.h"
#include "runtime/save.h"
#include "runtime/ui.h"
#include "support/file_watch.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

/* What shown documents read and act on. Only `ecs` is required: without
 * `model` documents show as authored, without `entities` activations reach no
 * one, and without `settings` data-setting elements stay as authored. A
 * preview shows documents their GameUi keeps hidden, as an editor previews
 * a dialog before anything opens it; `revealed` shows one entity's the same
 * way, as the editor shows the object selected in a scene it edits. */
typedef struct ui_world
{
    ecs_world *ecs;
    game_ui *model;
    game_entities *entities;
    game_save *settings;
    bool preview;
    uint64_t revealed; /* an entity (ecs_entity_to_u64), or 0 for none */
} ui_world;

/* ---------- Mounts ---------- */

/* One ui asset shown inside a host element: content/ui/<name>.yaml expanded
 * with its widgets, re-read within a second of an edit to either, keeping the
 * last valid version. The host element gets data-ui="<name>", and the mount's
 * RCSS is scoped to it; the host installs that RCSS in the element's
 * document. Games and the editor host mounts the same way. */
typedef struct ui_mount ui_mount;

/* A data-event activation: the event, the element's id ("" when it has none)
 * and a changed input's value. */
typedef struct ui_activation
{
    char event[128];
    char id[128];
    bool has_value;
    float value;
} ui_activation;

ui_mount *ui_mount_new(const char *content_root, const char *name);
void ui_mount_free(ui_mount *mount);
const char *ui_mount_name(const ui_mount *mount);
/* Shows the asset in `root`, moving it there when the root changes, and
 * reloads it when it or a widget changed. `listener` hears the root's
 * activations for ui_mount_listen. Returns true when the RCSS changed. */
bool ui_mount_sync(ui_mount *mount, rml_element *root, rml_listener *listener, double time);
/* The scoped RCSS of the last valid asset, "" before one loads. */
const char *ui_mount_style(const ui_mount *mount);
/* The last read error, "" when the asset loaded. */
const char *ui_mount_error(const ui_mount *mount);
/* Whether `element` is the mount's root or inside it. */
bool ui_mount_holds(const ui_mount *mount, rml_element *element);
/* Records the activation an event in the mount's root makes, if any: a click
 * on a data-event element, a change or submit of a data-event input, or a key
 * bound by data-keys="Key:Event ..." on the focused element or an ancestor
 * (keys are A-Z, 0-9, Space, Enter, Escape, Tab, Backspace, Delete, arrows as
 * Up/Down/Left/Right, Home, End, PageUp and PageDown). An installed asset
 * focuses its first autofocus element. */
void ui_mount_listen(ui_mount *mount, const rml_event *event);
/* Takes the oldest activation; false when there is none. */
bool ui_mount_take(ui_mount *mount, ui_activation *out);
/* Applies what behaviors set for `entity` in `model` since the last call. */
void ui_mount_apply(ui_mount *mount, game_ui *model, uint64_t entity);
/* Binds each data-setting element to that key in `settings`, both ways. An
 * element with a value attribute (a radio, or a widget used with one) is an
 * option: checked while the setting is its value, and choosing it sets it. A
 * checkbox holds "true" or "false", and other inputs and selects their value.
 * Refreshes the elements when a key changes. */
void ui_mount_settings(ui_mount *mount, game_save *settings);

/* ---------- Themes ---------- */

/* A project's theme: its ui/Theme.rcss and the files it imports (see
 * support/rcss.h), which style every UI document the project shows, unscoped
 * and before each document's own RCSS. The Studio project's theme is the
 * editor's. Re-read within a second of an edit to any of its files, keeping
 * the last valid version. Zero-initialize. */
typedef struct ui_theme
{
    file_watch *watches; /* Theme.rcss, then each file it imports */
    char *rcss;          /* NULL while the project has no theme */
    char error[256]; /* why the files are not the theme in use; "" when they are */
} ui_theme;

/* Re-reads the theme when one of its files changed and returns true; `rcss`
 * then holds the new theme, or the last valid one with `error` set. */
bool ui_theme_sync(ui_theme *theme, const char *content_root, double time);
void ui_theme_free(ui_theme *theme);

/* ---------- Game host ---------- */

/* Presents a world's GameUi documents over a canvas: one mount per entity in
 * a single document, stacked by GameUi.order. It applies what behaviors set
 * in the game_ui model and sends data-event activations to their entities.
 * Game UI passes the pointer through except over data-event elements and
 * inputs, or where RCSS sets pointer-events: auto. RCSS dp units scale with
 * the canvas height, 720 pixels being 1. */
typedef struct render_ui render_ui;

render_ui *render_ui_new(const char *content_root);
void render_ui_free(render_ui *ui);
/* Shows `world`'s GameUi documents. An input's change first stores its value
 * in the model under `#id` for Get UI Value. */
void render_ui_update(render_ui *ui, const ui_world *world, int width, int height, double dt);
/* Shows the host's ui asset `name` above the world's documents with `state`
 * as its root's class, or hides it when `state` is NULL. Its activations wait
 * for render_ui_take_overlay_event instead of reaching the world. */
void render_ui_set_overlay(render_ui *ui, const char *name, const char *state);
/* Takes the overlay's last activated event name; false when there is none. */
bool render_ui_take_overlay_event(render_ui *ui, char *name, size_t size);
/* Pointer and key input in canvas pixels. Returns what the documents
 * consumed, as RML_INPUT_CAPTURE_* flags. */
unsigned render_ui_input(render_ui *ui, const rml_input *input);
/* Draws the documents over `target` without clearing it. */
void render_ui_composite(render_ui *ui, CF_Canvas target);
/* The first mount's error, or "" when every shown asset loaded. */
const char *render_ui_error(const render_ui *ui);

#endif
