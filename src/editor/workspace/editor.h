#ifndef CHIBI_EDITOR_H
#define CHIBI_EDITOR_H

#include "editor/graph/eval_view.h"
#include "editor/ui/choices.h"
#include "editor/ui/flags.h"
#include "graph/model/behavior_asset.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/runtime/instance.h"

typedef struct editor editor;

/* Checkpoints own no runtime pointers. Export returns malloc-owned JSON; import
 * is for a fresh candidate editor and rejects unsupported or invalid state. */
void editor_initialize_fonts();
char *editor_checkpoint(editor *e);
bool editor_restore_checkpoint(editor *e, const char *json);

/* Host identifies packed 0xRRGGBBAA color inputs. */
typedef bool (*editor_color_resolver)(const node *, int output, int pin, void *user);

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
    const behavior_instance *inspection_instance;
    bool inspection_blocked;
    const char *preview_error;
    bool can_go_back;
    float zoom;
    float pan_x;
    float pan_y;
    bool camera_moving;
} editor_query;

/* Typed automation seam. UI mutations and these commands use the same editor
 * mutation helpers, history, validation, and persistence implementation. */
int editor_command_apply(editor *e, editor_command command);
void editor_query_state(const editor *e, editor_query *out);

typedef struct
{
    int exec_pins;
    int data_pins;
    int self_labels;
    float title_x;
    float title_y;
    float top;
    float bottom;
    float left;
    float right;
    float title_bottom;
    bool selected;
} editor_node_ui;

/* Pins actually drawn inside the canvas clip rectangle in the current frame. */
bool editor_query_node_ui(const editor *e, uint64_t node_id, editor_node_ui *out);
typedef struct
{
    float x;
    float y;
} editor_pin_ui;
bool editor_query_pin_ui(const editor *e, uint64_t pin_id, editor_pin_ui *out);
typedef struct
{
    bool traversed;
    double timestamp;
    float opacity;
    float bubble_opacity;
} editor_edge_activity;

/* Runtime activity used to draw an edge, without selection or hover styling. */
void editor_query_edge_activity(const editor *e, const edge *edge, editor_edge_activity *out);
/* Includes curved return wires and authored reroutes, in the current canvas. */
bool editor_graph_wires_visible(const editor *e);

/* The editor takes ownership of asset and frees it in editor_free(). */
editor *editor_new(behavior_asset *asset);
void editor_free(editor *e);

/* Returns the editor-owned working document without transferring ownership. */
const behavior_asset *editor_behavior_asset(const editor *e);

enum
{
    EDITOR_LOCATION_CALLER_LIMIT = 64
};

typedef struct
{
    uint64_t graph_id;
    uint64_t node_id;
    float pan_x;
    float pan_y;
    float zoom;
    int entry_index;
    bool fit;
} editor_caller_location;

typedef struct
{
    uint64_t graph_id;
    uint64_t *nodes; /* capture allocates; caller frees */
    int node_count;
    float pan_x;
    float pan_y;
    float zoom;
    bool fit;
    int entry_index;
    int caller_count;
    editor_caller_location callers[EDITOR_LOCATION_CALLER_LIMIT];
} editor_location;

editor_location editor_capture_location(const editor *e);
bool editor_can_visit_location(
    const editor *e,
    const behavior_asset *source,
    const char *key,
    const editor_location *location
);
int editor_restore_location(editor *e, const editor_location *location);
/* Workspace visits retain document Undo/Redo and working values by stable key. */
int editor_visit_behavior_asset(
    editor *e,
    const behavior_asset *source,
    uint64_t graph_id,
    const char *key
);

/* Explicit Reload replaces working data/history and keeps its workspace identity. */
int editor_reload_behavior_asset(
    editor *e,
    const behavior_asset *source,
    uint64_t graph_id,
    const char *key
);
/* Reloads a cached document without changing the visible document. */
int editor_reload_behavior_asset_document(editor *e, const behavior_asset *source, const char *key);

/* Replaces the editable Behavior Asset with a copy of source and opens graph_id. */
int editor_open_behavior_asset(editor *e, const behavior_asset *source, uint64_t graph_id);
/* Opens a graph and arranges/frames it on its next visible draw. Position changes
 * are undoable. Reopening the active graph preserves its current layout/view. */
int editor_open_graph(editor *e, uint64_t graph_id);
/* Delete a graph; generated expression definitions also remove their calls. */
int editor_delete_graph(editor *e, uint64_t graph_id);
/* Drill into an authored call; Back restores the caller view and selects the call. */
int editor_open_graph_call(editor *e, uint64_t node_id);
int editor_graph_back(editor *e);
/* Caller history is ordered oldest first. Ancestors restore their call-site view. */
int editor_graph_history_count(const editor *e);
uint64_t editor_graph_history_id(const editor *e, int index);
int editor_graph_ancestor(editor *e, int index);
void editor_set_value_formatter(editor *e, editor_value_formatter formatter, void *user);
void editor_set_flags_resolver(editor *e, editor_flags_resolver resolver, void *user);
void editor_set_choices_resolver(editor *e, editor_choices_resolver resolver, void *user);
void editor_set_color_resolver(editor *e, editor_color_resolver resolver, void *user);
const graph *editor_active_graph(const editor *e);

/* Creates the host dockspace before other dockable windows are submitted. */
void editor_prepare_docking(void);

/* Chooses which editor-owned side panels and canvas are submitted. */
void editor_set_ui_context(editor *e, editor_ui_context context);
bool editor_has_inspection_selection(const editor *e);
void editor_set_inspector_active(editor *e, bool active);
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
void editor_set_asset_opener(editor *e, int (*open)(void *, const char *, uint64_t), void *user);
void editor_set_prototype_catalog(editor *e, const char *const *names, int count);
void editor_set_asset_catalog(editor *e, const behavior_asset *const *assets, int count);

/* Renders the canvas and handles input. Call once per frame after
 * editor_prepare_docking(), between cf_app_update() and cf_app_draw_onto_screen(). */
void editor_draw(editor *e);
/* Draw behavior members inside the host-owned Navigator Outline tab. */
void editor_draw_outline(editor *e);

/* Advances the editor-owned preview instance with a host-supplied monotonic
 * timestamp, then advances editor state. Call once per host frame. */
void editor_tick(editor *e, double monotonic_time);

/* Returns the revision of the last committed authoring edit. */
unsigned editor_edit_revision(const editor *e);
/* UI edits may precede the next tick that advances the committed revision. */
bool editor_has_pending_edits(const editor *e);
void editor_undo(editor *e);
void editor_redo(editor *e);

/* Selects and centers a node in the canvas. */
void editor_select_node(editor *e, node *n);
void editor_reveal_node(editor *e, node *n);
int editor_reveal_node_by_id(editor *e, uint64_t graph_id, uint64_t node_id);

/* Adds a node of type desc to the graph at canvas (x, y) and returns the core
 * node for wiring, or NULL on allocation failure. */
node *editor_spawn(editor *e, const node_desc *desc, float x, float y);
node *editor_add_behavior_expression(editor *e, const char *source, float x, float y, behavior_expression_error *error);
int editor_set_behavior_expression(editor *e, node *call, const char *source, behavior_expression_error *error);

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
/* Disable live overlays and standalone fallback while a debug target is unavailable or stale. */
void editor_set_inspection_blocked(editor *e, bool blocked);
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
