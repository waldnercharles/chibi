#ifndef CBLUEPRINT_EDITOR_H
#define CBLUEPRINT_EDITOR_H

#include "../core/behavior_asset.h"
#include "../core/instance.h"

typedef struct editor editor;

typedef enum
{
    EDITOR_UI_CONTEXT_SCENE,
    EDITOR_UI_CONTEXT_PROTOTYPE,
    EDITOR_UI_CONTEXT_BEHAVIOR,
    EDITOR_UI_CONTEXT_SYSTEM,
} editor_ui_context;

typedef int (*editor_save_handler)(editor *e, void *user);

typedef enum
{
    EDITOR_COMMAND_CREATE_GRAPH,
    EDITOR_COMMAND_RENAME_GRAPH,
    EDITOR_COMMAND_OPEN_GRAPH,
    EDITOR_COMMAND_SPAWN_NODE,
    EDITOR_COMMAND_SET_PIN_DEFAULT,
    EDITOR_COMMAND_CONNECT_DATA,
    EDITOR_COMMAND_CONNECT_EXEC,
    EDITOR_COMMAND_COMPILE,
    EDITOR_COMMAND_SAVE,
    EDITOR_COMMAND_LOAD,
} editor_command_kind;

typedef struct
{
    editor_command_kind kind;
    union
    {
        struct
        {
            graph_kind graph_kind;
            const char *name;
        } create_graph;
        struct
        {
            uint64_t graph_id;
            const char *name;
        } rename_graph;
        struct
        {
            uint64_t graph_id;
        } open_graph;
        struct
        {
            const node_desc *desc;
            float x;
            float y;
        } spawn_node;
        struct
        {
            uint64_t node_id;
            int output;
            int pin_index;
            const pin_value *value;
        } set_pin_default;
        struct
        {
            uint64_t from_node;
            int from_pin;
            uint64_t to_node;
            int to_pin;
        } connect;
        struct
        {
            const char *path;
        } file;
    } data;
} editor_command;

typedef struct
{
    const graph *active_graph;
    int graph_count;
    int graph_visible;
    int dirty;
    const compile_report *compile;
} editor_query;

/* Typed automation seam. UI mutations and these commands use the same editor
 * mutation helpers, history, validation, and persistence implementation. */
int editor_command_apply(editor *e, editor_command command);
void editor_query_state(const editor *e, editor_query *out);

/* The editor takes ownership of bp and frees it in editor_free(). */
editor *editor_new(behavior_asset *bp);
void editor_free(editor *e);

/* Returns the editor-owned working document without transferring ownership. */
const behavior_asset *editor_behavior_asset(const editor *e);

/* Replaces the editable Behavior Asset with a copy of source and opens graph_id. */
int editor_open_behavior_asset(editor *e, const behavior_asset *source, uint64_t graph_id);
/* Opens a graph in the editor without changing Behavior Asset runtime semantics. */
int editor_open_graph(editor *e, uint64_t graph_id);
const graph *editor_active_graph(const editor *e);

/* Creates the host dockspace before other dockable windows are submitted. */
void editor_prepare_docking(void);

/* Chooses which editor-owned side panels and canvas are submitted. */
void editor_set_ui_context(editor *e, editor_ui_context context);
void editor_set_graph_visible(editor *e, int visible);
void editor_set_save_handler(editor *e, editor_save_handler handler, void *user);

/* ENG-3: true while a panel is consuming the keyboard, so the host must not
 * also route those keys to a focused viewport or to the graph canvas. */
int editor_host_captures_keyboard(void);

/* The fixed event signatures supplied by the host. Core keeps lifecycle names
 * out of its runtime vocabulary; the editor uses this catalog when it creates
 * event nodes and their pins. */
typedef struct
{
    const char *name;
    pin_type type;
    const char *type_name;
} editor_event_param;

typedef struct
{
    const char *name;
    const editor_event_param *params;
    int param_count;
} editor_event_signature;

/* `events` is borrowed and must outlive the editor. */
void editor_set_event_catalog(editor *e, const editor_event_signature *events, int count);

/* Renders the canvas and handles input. Call once per frame after
 * editor_prepare_docking(), between cf_app_update() and cf_app_draw_onto_screen(). */
void editor_draw(editor *e);

/* Advances the editor-owned preview instance with a host-supplied monotonic
 * timestamp, then advances editor state. Call once per host frame. */
void editor_tick(editor *e, double monotonic_time);

/* Returns the revision of the last committed authoring edit. */
unsigned editor_edit_revision(const editor *e);
void editor_undo(editor *e);
void editor_redo(editor *e);

/* Selects and centers a node in the canvas. */
void editor_select_node(editor *e, node *n);
void editor_reveal_node(editor *e, node *n);
int editor_reveal_node_by_id(editor *e, uint64_t graph_id, uint64_t node_id);

/* Adds a node of type desc to the graph at canvas (x, y) and returns the core
 * node for wiring, or NULL on allocation failure. */
node *editor_spawn(editor *e, const node_desc *desc, float x, float y);

/* Save the behavior_asset to `path` as JSON and update the editor's saved
 * baseline used by dirty queries. Returns 0 on success, -1 on error. */
int editor_save(editor *e, const char *path);

/* Replace the editor's behavior_asset with the JSON at `path`. Returns 0 on
 * success, -1 on error (the existing behavior_asset is left untouched). */
int editor_load(editor *e, const char *path);

/* Preview instance (UI-54/55/57). The editor owns a runtime instance over a
 * cloned applied snapshot. Authoring mutations stay on the working Behavior
 * Asset; an explicit reset replaces the snapshot. */
behavior_instance *editor_preview_instance(const editor *e);

/* Borrow a runtime instance for read-only graph inspection. The caller owns
 * the instance and must clear or replace it before the instance is detached or
 * destroyed. */
void editor_set_inspection_instance(editor *e, const behavior_instance *instance);
void editor_set_inspection_run(editor *e, const flow_run *run);
void editor_set_inspection_trace(editor *e, const behavior_instance_trace_entry *trace, int count);
/* Updates render-only activity for a host execution path without tracing nodes. */
void editor_set_execution_pulse(editor *e, double monotonic_time);

/* Monotonic counter bumped on every instance (re)creation, so the host can
 * dispatch a once-per-instance event such as the demo Begin() convention. */
unsigned editor_preview_generation(const editor *e);

/* Create the preview runtime if missing. No-op when one is attached. */
void editor_preview_create(editor *e);
void editor_preview_reset(editor *e);

/* Dispatch an authored event through the selected inspection instance, or
 * the editor preview when no runtime instance is selected. */
flow_run *editor_dispatch(editor *e, const char *name, flow_inputs inputs, flow_error *error);
/* Dispatch only through the editor preview, regardless of inspection state. */
flow_run *editor_dispatch_preview(editor *e, const char *name, flow_inputs inputs, flow_error *error);

#endif
