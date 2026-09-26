#ifndef CHIBI_EDITOR_H
#define CHIBI_EDITOR_H

#include "editor/graph/eval_view.h"
#include "editor/ui/choices.h"
#include "graph/document/events.h"
#include "graph/model/behavior_asset.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/runtime/instance.h"

typedef struct editor editor;
struct ui_event;

/* Checkpoints own no runtime pointers. Export returns malloc-owned JSON; import
 * is for a fresh candidate editor and rejects unsupported or invalid state. */
char *editor_checkpoint(editor *e);
bool editor_restore_checkpoint(editor *e, const char *json);

typedef enum
{
    EDITOR_UI_CONTEXT_SCENE,
    EDITOR_UI_CONTEXT_PROTOTYPE,
    EDITOR_UI_CONTEXT_BEHAVIOR,
    EDITOR_UI_CONTEXT_SYSTEM,
} editor_ui_context;

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
    } data;
} editor_command;

typedef struct
{
    const graph *active_graph;
    int graph_count;
    int dirty;
    const compile_report *compile;
    const behavior_instance *inspection_instance;
    bool inspection_blocked;
    const char *preview_error;
} editor_query;

/* Typed automation seam. UI mutations and these commands use the same editor
 * mutation helpers, history, validation, and persistence implementation. */
int editor_command_apply(editor *e, editor_command command);
void editor_query_state(const editor *e, editor_query *out);

typedef struct
{
    bool traversed;
    double timestamp;
    float opacity; /* execution heat; zero once it has cooled */
} editor_edge_activity;

/* Runtime activity used to draw an edge, without selection or hover styling. */
void editor_query_edge_activity(const editor *e, const edge *edge, editor_edge_activity *out);

/* The editor takes ownership of asset and frees it in editor_free(). */
editor *editor_new(behavior_asset *asset);
void editor_free(editor *e);

/* Returns the editor-owned working document without transferring ownership. */
const behavior_asset *editor_behavior_asset(const editor *e);

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
/* Drops a cached document whose asset is gone, as after a delete. */
void editor_forget_behavior_asset(editor *e, const char *key);
/* A document, open or cached, keeps its history under its asset's new name. */
void editor_rename_behavior_asset(editor *e, const char *from, const char *to);

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
/* Callers that Back can return to, oldest first. */
int editor_graph_history_count(const editor *e);
/* Returns to the caller at index, dropping the calls entered after it. */
int editor_graph_ancestor(editor *e, int index);
void editor_set_value_formatter(editor *e, editor_value_formatter formatter, void *user);
void editor_set_choices_resolver(editor *e, editor_choices_resolver resolver, void *user);
const graph *editor_active_graph(const editor *e);

/* Chooses the node catalog and graph rules for the edited document. */
void editor_set_ui_context(editor *e, editor_ui_context context);

/* ENG-3: true while the retained UI consumes the keyboard, so the host must
 * not also route those keys to a focused viewport. */
int editor_host_captures_keyboard(void);
/* A retained UI host reports its own keyboard capture here. */
void editor_set_retained_keyboard(bool captured);

/* The fixed event signatures supplied by the host. Core keeps lifecycle names
 * out of its runtime vocabulary; the editor uses this catalog when it creates
 * event nodes and their pins. */
/* `events` is borrowed and must outlive the editor. */
void editor_set_event_catalog(editor *e, const graph_host_event_desc *events, int count);
/* The events the GameUi documents showing the asset's entities send it
 * (assets/ui_events.h), offered as Event nodes; borrowed until the next call. */
void editor_set_ui_event_catalog(editor *e, const struct ui_event *events, int count);
void editor_set_asset_opener(editor *e, int (*open)(void *, const char *, uint64_t), void *user);
void editor_set_prototype_catalog(editor *e, const char *const *names, int count);
/* `assets` stay borrowed until the next call, so a host that frees one sets
 * the catalog again before the editor runs. */
void editor_set_asset_catalog(editor *e, const behavior_asset *const *assets, int count);

/* Advances the editor-owned preview instance with a host-supplied monotonic
 * timestamp, then advances editor state. Call once per host frame. */
void editor_tick(editor *e, double monotonic_time);

/* Returns the revision of the last committed authoring edit. */
unsigned editor_edit_revision(const editor *e);
/* UI edits may precede the next tick that advances the committed revision. */
bool editor_has_pending_edits(const editor *e);
void editor_undo(editor *e);
void editor_redo(editor *e);
bool editor_can_undo(const editor *e);
bool editor_can_redo(const editor *e);

/* Behavior output from the preview, oldest first. The origin label is written
 * to origin; the message stays valid until the log changes. */
typedef struct
{
    double time;
    unsigned long step;
    const char *message;
} editor_log_line;
int editor_log_count(const editor *e);
editor_log_line editor_log_at(const editor *e, int index, char *origin, size_t origin_size);
void editor_clear_log(editor *e);

/* Adds a node of type desc to the graph at canvas (x, y) and returns the core
 * node for wiring, or NULL on allocation failure. */
node *editor_spawn(editor *e, const node_desc *desc, float x, float y);
node *editor_add_behavior_expression(
    editor *e,
    const char *source,
    float x,
    float y,
    behavior_expression_error *error
);
int editor_set_behavior_expression(editor *e, node *call, const char *source, behavior_expression_error *error);
/* Renames an expression input in its source, keeping its wire or binding. */
int editor_rename_behavior_expression_input(
    editor *e,
    node *call,
    const char *from,
    const char *to,
    behavior_expression_error *error
);
/* Sets source as a rename of from to to, so from's wire or binding moves to to. */
int editor_rewire_behavior_expression(
    editor *e,
    node *call,
    const char *source,
    const char *from,
    const char *to,
    behavior_expression_error *error
);

/* Borrow a runtime instance for read-only graph inspection. The caller owns
 * the instance and must clear or replace it before the instance is detached or
 * destroyed. */
void editor_set_inspection_instance(editor *e, const behavior_instance *instance);
void editor_set_inspection_run(editor *e, const flow_run *run);
void editor_set_inspection_trace(editor *e, const behavior_instance_trace_entry *trace, int count);
/* Disable live overlays and standalone fallback while a debug target is unavailable or stale. */
void editor_set_inspection_blocked(editor *e, bool blocked);

/* Preview instance (UI-54/55/57). The editor owns a runtime instance over a
 * cloned applied snapshot. Authoring mutations stay on the working Behavior
 * Asset; an explicit reset replaces the snapshot. Create is a no-op when one is
 * attached. */
void editor_preview_create(editor *e);
void editor_preview_reset(editor *e);

#endif
