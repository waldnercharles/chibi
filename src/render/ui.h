#ifndef CHIBI_RENDER_UI_H
#define CHIBI_RENDER_UI_H

#include "ecs/ecs.h"
#include "execution/arguments.h"
#include "platform/cute.h"
#include "render/skip.h"
#include "render/ui_loader.h"
#include "runtime/save.h"
#include "runtime/ui.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

/* Event hooks are copied with each world view. Context and names returned by
 * payload remain borrowed through that invocation. A negative payload count
 * rejects an unsupported declaration before dispatch. A negative dispatch
 * status is a failure, which the optional failure hook describes. */
typedef struct
{
    const char *name;
    pin_value type;
} render_ui_event_field;
typedef struct
{
    void *context;
    int (*payload)(void *, const char *, render_ui_event_field *, int capacity);
    int (*dispatch)(void *, uint64_t entity, const char *, execution_arguments);
    bool (*initializing)(void *);
    const char *(*failure)(void *);
} render_ui_host;

/* What shown documents read and act on. Only `ecs` is required: without
 * `page` documents show on a bare page, without `model` as authored, without
 * `live` activations reach no one, and without `save` documents bind an
 * empty `save`. A preview shows documents their Ui keeps hidden, as an
 * editor previews a dialog before anything opens it; `revealed` shows one
 * entity's the same way, as the editor shows the object selected in a scene
 * it edits, and `hidden` none of the entities an editor hides. */
typedef struct ui_world
{
    ecs_world *ecs;
    const char *page; /* the scene's root ui document (ui_page) */
    ui *model;
    render_ui_host host; /* typed event delivery; an empty record disables it */
    save *save;
    bool preview;
    uint64_t revealed; /* an entity (ecs_entity_to_u64), or 0 for none */
    render_skip hidden;
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
    save *save; /* the one `save` shows */
    uint64_t save_seen;
    uint64_t data_seen; /* the ui revision the entities' variables show */
} ui_data;

/* Creates the model in `context` over `doc`, which it takes, adding `save`
 * and `system` to its root; a NULL doc has no other variables. `emit` hears
 * emit('Event'). */
void ui_data_init(ui_data *data, rml_context *context, yyjson_mut_doc *doc, rml_emit_fn emit, void *user);
/* Saves in `save` the keys bindings changed since the last sync, and no
 * others, so a key the save changed meanwhile keeps its value; then shows its
 * keys when they changed, and the system's theme. */
void ui_data_sync(ui_data *data, save *save);
/* Sets `value` to a copy of `text` unless it holds it already; the JSON keeps
 * what it copies until it is freed, so views update values in place. */
void ui_data_set_text(yyjson_mut_doc *doc, yyjson_mut_val *value, const char *text);
/* Set `object`'s member `name`, which it holds, in place; true when the value
 * changed, so a source reports a change only when a view would show one. */
bool ui_data_put_text(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *name, const char *text);
bool ui_data_put_bool(yyjson_mut_val *object, const char *name, bool on);
bool ui_data_put_int(yyjson_mut_val *object, const char *name, int number);
bool ui_data_put_number(yyjson_mut_val *object, const char *name, double number);
/* Binds `self` in the document mounted at `root` to `entity`'s data, which
 * behaviors set with Set UI Data and the model holds as the entity's own
 * variable, "e<entity>", so a change to one entity's data re-evaluates only
 * the views bound to it; call before the mount installs its markup. Binding
 * the same entity again changes nothing. */
void ui_data_bind_self(ui_data *data, rml_element *root, uint64_t entity);
/* Shows what behaviors set with Set UI Data since the last call, changing
 * values in place so views keep their bindings. */
void ui_data_show_entities(ui_data *data, const ui *model);
/* Drops an entity's data, as when its document closes. */
void ui_data_forget_entity(ui_data *data, uint64_t entity);
void ui_data_free(ui_data *data);

/* ---------- Mounts ---------- */

/* One expanded UI record shown inside a host element. Resident prepared
 * records are the default. An explicitly installed loader may refresh authored
 * records, keeping the last valid version on failure. The host element gets data-ui="<name>" and binds the
 * context's UI_DATA_MODEL, and the mount's RCSS is scoped to it; the host
 * installs that RCSS in the element's document. Games and the editor host
 * mounts the same way. Each element carries data-src="<asset>:<line>"
 * (ui_document_sources), and the mount records where each line of its RCSS
 * came from, so a host that installs it can trace its rules
 * (support/ui_style.h). */
typedef struct ui_mount ui_mount;

/* The arguments emit('Event', ...) passes after its name, as text; more are
 * dropped. */
#define UI_ACTIVATION_ARGS 4

/* An activation: the event emitted or bound to a key, the id of the element
 * whose binding sent it ("" when it has none), emit's arguments, and its
 * value: the first argument as a number, or an input's. The DOM event that
 * sent it gives the modifier keys (rml_key_modifier bits), the mouse button
 * and the pointer in context pixels. `anchor` holds the element whose binding
 * sent it, where a popover opens; it reads NULL once that element is gone,
 * and lives until the mount's next take or its free. */
typedef struct ui_activation
{
    char event[128];
    char id[128];
    int argc;
    char args[UI_ACTIVATION_ARGS][128];
    bool has_value;
    float value;
    unsigned modifiers;
    int button;
    float x, y;
    rml_element_ref *anchor;
} ui_activation;

/* Runs activation `a` on `entity`'s behaviors in `live` now, or on every
 * entity's for an entity of none, so what they set shows in the frame that
 * sent it. Its payload is what the event asset `live` holds under its name
 * declares (none for a local event), in order: Shift, Alt and Mod (bools;
 * `primary` holds Mod's modifier bits, Meta on a Mac and Control elsewhere)
 * and Pointer (CF_V2, in the document's pixels) come from the DOM event, as a
 * web handler reads event.shiftKey and event.clientX; each other value is
 * emit's next argument read as its type (text, a number or a flag). False,
 * running nothing, for a payload a UI event cannot carry; a dispatch that
 * fails is the host's to describe (failure). */
bool ui_activation_send(const render_ui_host *host, ecs_entity entity, const ui_activation *a, unsigned primary);
ui_mount *ui_mount_new(const char *content_root, const char *name);
void ui_mount_free(ui_mount *mount);
void ui_mount_set_loader(ui_mount *, const render_ui_loader *);
const char *ui_mount_name(const ui_mount *mount);
/* Shows the asset in `root`, moving it there when the root changes, and
 * reloads it when it or a widget changed. `listener` hears the root's keys
 * for ui_mount_listen. Returns true when the RCSS changed. */
bool ui_mount_sync(ui_mount *mount, rml_element *root, rml_listener *listener, double time);
/* Forget a conditional host before its element is removed from the tree. */
void ui_mount_detach(ui_mount *mount);
/* The scoped RCSS of the last valid asset, "" before one loads. */
const char *ui_mount_style(const ui_mount *mount);
/* The last read error, or what RmlUi rejected in the markup it installed,
 * such as a binding's expression; "" when the asset loaded. */
const char *ui_mount_error(const ui_mount *mount);
/* The element the mount shows its asset in, or NULL while detached. */
rml_element *ui_mount_root(const ui_mount *mount);
/* Whether `element` is the mount's root or inside it. */
bool ui_mount_holds(const ui_mount *mount, rml_element *element);
/* Records the activation a key down in the mount's root makes, if any: a key
 * bound by data-keys="Key:Event ..." on the focused element or an ancestor
 * (keys are A-Z, 0-9, Space, Enter, Escape, Tab, Backspace, Delete, arrows as
 * Up/Down/Left/Right, Home, End, PageUp and PageDown). An installed asset
 * focuses its first autofocus element. Text/select controls retain editing
 * keys unless bound on that control. Text inputs and textareas permit an
 * ancestor Escape binding. */
void ui_mount_listen(ui_mount *mount, const rml_event *event);
/* Records the activation key `key`, named as data-keys names it ("Left"),
 * sends while the area showing the mount has the keyboard but nothing in
 * it binds the key: data-area-keys="Key:Event ..." on its first element
 * with one, an editor's own keymap, as Blender's. False when none binds
 * it. */
bool ui_mount_area_key(ui_mount *mount, const char *key);
/* Records emit(`name`, args...) as sent by the binding `event` ran, on its
 * current element, which the mount holds. */
void ui_mount_emit(
    ui_mount *mount,
    const rml_event *event,
    const char *name,
    int argc,
    const char *const *args
);
/* Whether activations wait to be taken. */
bool ui_mount_pending(const ui_mount *mount);
/* Takes the oldest activation, releasing the anchor of the one taken before;
 * false when there is none. */
bool ui_mount_take(ui_mount *mount, ui_activation *out);
/* Applies what behaviors set for `entity` in `model` since the last call. */
void ui_mount_apply(ui_mount *mount, ui *model, uint64_t entity);
/* Records under #id, in dp, where each element with an id and a
 * data-event-scroll binding is scrolled, for Get UI Scroll. First it windows
 * the lists marked data-list-rows="<rows>" (ui/list.h, the List widget's
 * scroller): it sizes each for its rows, scrolls a row data-list-reveal
 * names (Reveal UI List Row) into view once, and sends `window` with
 * ev.first and ev.count when the rows to show change, so a document's
 * data-event-window="emit('ShowRows', ev.first, ev.count)" reaches its
 * behaviors before they refresh. */
void ui_mount_measure(ui_mount *mount, ui *model, uint64_t entity);

/* ---------- Pages ---------- */

/* A scene's root document, which a scene names with `ui:` and a project
 * gives its scenes by default: the page the scene's documents show on, as a
 * web page's root layout. Its RCSS, with the stylesheets it @imports (a
 * theme's tokens, say), styles the whole page unscoped; its markup is the
 * page body, which it may wrap in <body ...> to set the body's attributes;
 * and the scene's documents show in its <slot/>, or after its markup.
 * Resident prepared records are the default; an explicit loader owns authored
 * refresh and dependency tracking. Zero-initialize. */
typedef struct ui_page
{
    char name[128]; /* the ui asset shown; "" for none */
    char *content_root;
    render_ui_loader loader;
    void *source;
    uint64_t prepared_revision;
    bool prepared_only, prepared_loaded;
    ui_document document; /* its last valid version, RCSS unscoped */
    char error[256]; /* why the asset is not the version shown; "" when it is */
} ui_page;

/* Shows ui asset `name` ("" for none), re-reading it when it or an import
 * changed; true when `document` changed. */
bool ui_page_sync(ui_page *page, const char *content_root, const char *name, double time);
void ui_page_free(ui_page *page);
void ui_page_set_loader(ui_page *, const render_ui_loader *);

/* ---------- Game host ---------- */

/* Presents a world's Ui documents over a canvas: one mount per entity in
 * the world's page, stacked by Ui.order. It applies what behaviors set
 * in the ui model and sends activations to their entities. Its style
 * sheet is named "game-ui-<n>" (support/ui_style.h), and the page's elements
 * carry data-src as a mount's do. Game UI passes the pointer through except
 * over data-event-click elements and inputs, or where RCSS sets
 * pointer-events: auto. RCSS dp units scale with the canvas height, 720
 * pixels being 1. */
typedef struct render_ui render_ui;
void render_ui_set_loader(render_ui *, const render_ui_loader *);

/* `data`, NULL for none, gives the documents its root's members as
 * variables beside `save` and `system` (ui_data_init), so a caller can show
 * them its own state; the new host takes it. */
render_ui *render_ui_new(const char *content_root, yyjson_mut_doc *data);
/* Resident catalog only. Missing records never load project files. */
render_ui *render_ui_new_prepared(yyjson_mut_doc *data);
void render_ui_free(render_ui *ui);
/* The documents' data model, in which the host shows the variables it gave
 * render_ui_new. */
ui_data *render_ui_data(render_ui *ui);
/* Shows `world`'s Ui documents. First it sends the documents' activations
 * to their entities' behaviors (ui_activation_send), an input's change
 * storing its value in the model under `#id` for Get UI Value, so what they
 * set shows in this update; they wait for the next while a spawn
 * initializes. */
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
/* The same for one region of the documents' canvas, x0 y0 x1 y1 as
 * fractions of its size from the top left, stretched over `target`. */
void render_ui_composite_region(render_ui *ui, CF_Canvas target, const float region[4]);
/* The dp the canvas's height spans, 720 by default and for 0: a host that
 * shows a small document large lays it out on a smaller screen. */
void render_ui_set_reference_height(render_ui *ui, float dp);
/* The region, in those fractions, that the overlay's content covers after
 * the last update; false when it shows nothing. A host frames a document's
 * content with it, as a thumbnail frames a widget. */
bool render_ui_overlay_bounds(const render_ui *ui, float bounds[4]);
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
