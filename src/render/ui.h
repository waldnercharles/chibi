#ifndef CHIBI_RENDER_UI_H
#define CHIBI_RENDER_UI_H

#include "assets/ui_document.h"
#include "ecs/ecs.h"
#include "platform/cute.h"
#include "runtime/behaviors/entities.h"
#include "runtime/save.h"
#include "runtime/ui.h"
#include "support/file_watch.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

/* What shown documents read and act on. Only `ecs` is required: without
 * `page` documents show on a bare page, without `model` as authored, without
 * `entities` activations reach no one, and without `save` documents bind an
 * empty `save`. A preview shows documents their GameUi keeps hidden, as an
 * editor previews a dialog before anything opens it; `revealed` shows one
 * entity's the same way, as the editor shows the object selected in a scene
 * it edits. */
typedef struct ui_world
{
    ecs_world *ecs;
    const char *page; /* the scene's root ui document (ui_page) */
    game_ui *model;
    game_entities *entities;
    game_save *save;
    bool preview;
    uint64_t revealed; /* an entity (ecs_entity_to_u64), or 0 for none */
} ui_world;

/* ---------- Data ---------- */

/* The data model every mount binds, with RmlUi's data bindings (see rml.h):
 * {{ save.name }}, data-for, data-value="save.volume",
 * data-checked="save.theme", data-event-click="emit('Close')". */
#define UI_DATA_MODEL "ui"

/* A host's data model. Its variables are the members of `doc`'s root, among
 * them `save`: the world's saved keys as text, "true" and "false" as
 * booleans, which bindings may set; a key nothing saved, or removed, reads
 * as empty until a binding sets it. `system.dark` tells whether the
 * system's theme is dark, as CSS's prefers-color-scheme does. */
typedef struct ui_data
{
    yyjson_mut_doc *doc;
    rml_data_model *model;
    game_save *save; /* the one `save` shows */
    uint64_t save_seen;
} ui_data;

/* Creates the model in `context` over `doc`, which it takes, adding `save`
 * and `system` to its root; a NULL doc has no other variables. `emit` hears
 * emit('Event'). */
void ui_data_init(ui_data *data, rml_context *context, yyjson_mut_doc *doc, rml_emit_fn emit, void *user);
/* Saves in `save` the keys bindings changed since the last sync, and no
 * others, so a key the save changed meanwhile keeps its value; then shows its
 * keys when they changed, and the system's theme. */
void ui_data_sync(ui_data *data, game_save *save);
/* Sets `value` to a copy of `text` unless it holds it already; the JSON keeps
 * what it copies until it is freed, so views update values in place. */
void ui_data_set_text(yyjson_mut_doc *doc, yyjson_mut_val *value, const char *text);
void ui_data_free(ui_data *data);

/* ---------- Mounts ---------- */

/* One ui asset shown inside a host element: content/ui/<name>.yaml expanded
 * with its widgets, re-read within a second of an edit to either, keeping the
 * last valid version. The host element gets data-ui="<name>" and binds the
 * context's UI_DATA_MODEL, and the mount's RCSS is scoped to it; the host
 * installs that RCSS in the element's document. Games and the editor host
 * mounts the same way. Each element carries data-src="<asset>:<line>"
 * (ui_document_sources), and the mount records where each line of its RCSS
 * came from, so a host that installs it can trace its rules
 * (support/ui_style.h). */
typedef struct ui_mount ui_mount;

/* An activation: the event emitted or bound to a key, the id of the element
 * whose binding sent it ("" when it has none), and its value: emit's second
 * argument, or an input's. */
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
 * reloads it when it or a widget changed. `listener` hears the root's keys
 * for ui_mount_listen. Returns true when the RCSS changed. */
bool ui_mount_sync(ui_mount *mount, rml_element *root, rml_listener *listener, double time);
/* The scoped RCSS of the last valid asset, "" before one loads. */
const char *ui_mount_style(const ui_mount *mount);
/* The last read error, or what RmlUi rejected in the markup it installed,
 * such as a binding's expression; "" when the asset loaded. */
const char *ui_mount_error(const ui_mount *mount);
/* Whether `element` is the mount's root or inside it. */
bool ui_mount_holds(const ui_mount *mount, rml_element *element);
/* Records the activation a key down in the mount's root makes, if any: a key
 * bound by data-keys="Key:Event ..." on the focused element or an ancestor
 * (keys are A-Z, 0-9, Space, Enter, Escape, Tab, Backspace, Delete, arrows as
 * Up/Down/Left/Right, Home, End, PageUp and PageDown). An installed asset
 * focuses its first autofocus element. */
void ui_mount_listen(ui_mount *mount, const rml_event *event);
/* Records `event` as emitted by the binding on `element`, which the mount
 * holds, with `value`, emit's second argument, as its value, or else an
 * input's; NULL for none. */
void ui_mount_emit(ui_mount *mount, rml_element *element, const char *event, const char *value);
/* Takes the oldest activation; false when there is none. */
bool ui_mount_take(ui_mount *mount, ui_activation *out);
/* Applies what behaviors set for `entity` in `model` since the last call. */
void ui_mount_apply(ui_mount *mount, game_ui *model, uint64_t entity);

/* ---------- Pages ---------- */

/* A scene's root document, which a scene names with `ui:` and a project
 * gives its scenes by default: the page the scene's documents show on, as a
 * web page's root layout. Its RCSS, with the stylesheets it @imports (a
 * theme's tokens, say), styles the whole page unscoped; its markup is the
 * page body, which it may wrap in <body ...> to set the body's attributes;
 * and the scene's documents show in its <slot/>, or after its markup.
 * Re-read within a second of an edit to it or an import, keeping the last
 * valid version. Zero-initialize. */
typedef struct ui_page
{
    char name[128]; /* the ui asset shown; "" for none */
    file_watch asset;
    file_watch *imports;  /* the stylesheets its RCSS imports */
    ui_document document; /* its last valid version, RCSS unscoped */
    char error[256]; /* why the asset is not the version shown; "" when it is */
} ui_page;

/* Shows ui asset `name` ("" for none), re-reading it when it or an import
 * changed; true when `document` changed. */
bool ui_page_sync(ui_page *page, const char *content_root, const char *name, double time);
void ui_page_free(ui_page *page);

/* ---------- Game host ---------- */

/* Presents a world's GameUi documents over a canvas: one mount per entity in
 * the world's page, stacked by GameUi.order. It applies what behaviors set
 * in the game_ui model and sends activations to their entities. Its style
 * sheet is named "game-ui-<n>" (support/ui_style.h), and the page's elements
 * carry data-src as a mount's do. Game UI passes the pointer through except
 * over data-event-click elements and inputs, or where RCSS sets
 * pointer-events: auto. RCSS dp units scale with the canvas height, 720
 * pixels being 1. */
typedef struct render_ui render_ui;

render_ui *render_ui_new(const char *content_root);
void render_ui_free(render_ui *ui);
/* Shows `world`'s GameUi documents. An input's change first stores its value
 * in the model under `#id` for Get UI Value. */
void render_ui_update(render_ui *ui, const ui_world *world, int width, int height, double dt);
/* Shows the host's ui asset `name` above the world's documents with `state`
 * as its root's class, or hides it when `state` is NULL, from the next
 * render_ui_update. Its activations wait for render_ui_take_overlay_event
 * instead of reaching the world. */
void render_ui_set_overlay(render_ui *ui, const char *name, const char *state);
/* Takes the overlay's last activated event name; false when there is none. */
bool render_ui_take_overlay_event(render_ui *ui, char *name, size_t size);
/* Pointer and key input in canvas pixels. Returns what the documents
 * consumed, as RML_INPUT_CAPTURE_* flags. */
unsigned render_ui_input(render_ui *ui, const rml_input *input);
/* Whether the focused element takes typed text, as a text input or a
 * textarea does. While one does, a host sends it the letters it would
 * otherwise act on itself, as a browser gives them to a focused field. */
bool render_ui_wants_text(const render_ui *ui);
/* Draws the documents over `target` without clearing it. */
void render_ui_composite(render_ui *ui, CF_Canvas target);
/* The first mount's error, or "" when every shown asset loaded. */
const char *render_ui_error(const render_ui *ui);
/* The RmlUi context the documents live in, for inspecting them. */
rml_context *render_ui_context(const render_ui *ui);

/* ---------- UI wiring (workstream K) ---------- */

/* Takes the overlay's oldest activation, with the element that sent it and
 * an input's value, as Storybook's Actions log them; false when there is
 * none. Use this or render_ui_take_overlay_event, not both. */
bool render_ui_take_overlay_activation(render_ui *ui, ui_activation *out);

#endif
