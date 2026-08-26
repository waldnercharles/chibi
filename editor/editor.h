#ifndef CBLUEPRINT_EDITOR_H
#define CBLUEPRINT_EDITOR_H

#include "../core/blueprint.h"
#include "../core/instance.h"

typedef struct editor editor;

/* The editor takes ownership of bp and frees it in editor_free(). */
editor *editor_new(blueprint *bp);
void editor_free(editor *e);

/* Returns the editor-owned working document without transferring ownership. */
const blueprint *editor_blueprint(const editor *e);

/* Replaces the editable Blueprint with a copy of source and opens graph_id. */
int editor_open_blueprint(editor *e, const blueprint *source, uint64_t graph_id);
/* Opens a graph in the editor without changing Blueprint runtime semantics. */
int editor_open_graph(editor *e, uint64_t graph_id);
const graph *editor_active_graph(const editor *e);

/* Creates the host dockspace before other dockable windows are submitted. */
void editor_prepare_docking(void);

/* ENG-3: true while a panel is consuming the keyboard, so the host must not
 * also route those keys to a focused viewport or to the graph canvas. */
int editor_host_captures_keyboard(void);

/* The events this host dispatches, with the parameter signature each one
 * carries. EVT-9 keeps core free of lifecycle meaning and EVT-10 makes these
 * host conventions, so the vocabulary is supplied rather than built in.
 *
 * The signature matters as much as the name: an event node's parameters are
 * matched positionally by type, and flow_event() substitutes a pin's default
 * for any parameter whose type does not match the dispatched value, silently.
 * Declaring the signature lets the editor create parameters that receive the
 * host's values instead of zeroes. */
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

/* `events` is borrowed and must outlive the editor. Free text stays available,
 * since a custom event's name and signature are arbitrary. */
void editor_set_event_catalog(editor *e, const editor_event_signature *events, int count);

/* Renders the canvas and handles input. Call once per frame after
 * editor_prepare_docking(), between cf_app_update() and cf_app_draw_onto_screen(). */
void editor_draw(editor *e);

/* Advances the editor-owned preview instance with a host-supplied monotonic
 * timestamp, then advances editor state. Call once per host frame. */
void editor_tick(editor *e, double monotonic_time);

/* Returns the revision of the last committed authoring edit. */
unsigned editor_edit_revision(const editor *e);

/* Selects and centers a node in the canvas. */
void editor_select_node(editor *e, node *n);
void editor_reveal_node(editor *e, node *n);
int editor_reveal_node_by_id(editor *e, uint64_t graph_id, uint64_t node_id);

/* Adds a node of type desc to the graph at canvas (x, y) and returns the core
 * node for wiring, or NULL on allocation failure. */
node *editor_spawn(editor *e, const node_desc *desc, float x, float y);

/* Save the blueprint to `path` as JSON. Returns 0 on success, -1 on error. */
int editor_save(const editor *e, const char *path);

/* Replace the editor's blueprint with the JSON at `path`. Returns 0 on
 * success, -1 on error (the existing blueprint is left untouched). */
int editor_load(editor *e, const char *path);

/* Preview instance (UI-54/55/56). The editor owns one blueprint_instance;
 * any authoring mutation destroys it at the edit choke point and releases the
 * definition's mutation guard. The host recreates it and drives dispatch. */
blueprint_instance *editor_preview_instance(const editor *e);

/* Borrow a runtime instance for read-only graph inspection. The caller owns
 * the instance and must clear or replace it before the instance is detached or
 * destroyed. */
void editor_set_inspection_instance(editor *e, const blueprint_instance *instance);
void editor_set_inspection_run(editor *e, const flow_run *run);
void editor_set_inspection_trace(editor *e, const blueprint_instance_trace_entry *trace, int count);

/* Monotonic counter bumped on every instance (re)creation, so the host can
 * dispatch a once-per-instance event such as the demo Begin() convention. */
unsigned editor_preview_generation(const editor *e);

/* Create the preview runtime if missing. No-op when one is attached. */
void editor_preview_create(editor *e);

/* Dispatch an authored event through the selected inspection instance, or
 * the editor preview when no runtime instance is selected. */
flow_run *editor_dispatch(editor *e, const char *name, flow_inputs inputs, flow_error *error);
/* Dispatch only through the editor preview, regardless of inspection state. */
flow_run *editor_dispatch_preview(editor *e, const char *name, flow_inputs inputs, flow_error *error);

#endif
