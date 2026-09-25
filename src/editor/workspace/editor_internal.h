#ifndef CHIBI_EDITOR_INTERNAL_H
#define CHIBI_EDITOR_INTERNAL_H

#include "editor/workspace/editor.h"

#define ED_HIST_MAX 64
#define ED_LOG_MAX 128
#define ED_LOG_MESSAGE 256

typedef enum ed_pin_kind
{
    ED_PIN_INPUT,
    ED_PIN_OUTPUT
} ed_pin_kind;

typedef struct ed_pin_view
{
    node *n;
    int kind;
    int index;
} ed_pin_view;

typedef struct ed_link_ref
{
    uint64_t from_id;
    uint64_t from_pin_id;
    uint64_t to_id;
    uint64_t to_pin_id;
} ed_link_ref;

typedef struct ed_log_entry
{
    const node *origin;
    double timestamp;
    unsigned long step;
    char message[ED_LOG_MESSAGE];
} ed_log_entry;

enum
{
    ED_CALLER_LIMIT = 64
};

/* A graph call the user drilled through; Back returns to it. */
typedef struct
{
    uint64_t graph_id;
    uint64_t node_id;
} ed_graph_caller;

/* Where a document was left: its graph, selection and the calls above it. */
typedef struct
{
    uint64_t graph_id;
    uint64_t *nodes; /* capture allocates; caller frees */
    int node_count;
    int caller_count;
    ed_graph_caller callers[ED_CALLER_LIMIT];
} ed_location;

typedef struct
{
    char *key;
    char *json;
    char *saved;
    uint64_t owner_identity;
    char **undo;
    char **redo;
    ed_location location;
    ed_link_ref *links;
} ed_document_history;

struct editor
{
    behavior_asset *asset;
    editor_ui_context ui_context;
    graph *active_graph;
    ed_graph_caller *graph_callers;
    eval_view *ev;
    editor_value_formatter value_formatter;
    void *value_formatter_user;
    editor_choices_resolver choices_resolver;
    void *choices_user;
    behavior_asset *preview_behavior_asset;
    behavior_instance *instance; /* preview instance, or NULL after an explicit reset */
    const behavior_instance *inspection_instance; /* borrowed runtime target */
    const flow_run *inspection_run; /* borrowed runtime activation */
    bool inspection_blocked;
    const behavior_instance_trace_entry *inspection_trace;
    int inspection_trace_count;
    unsigned edit_revision;
    const behavior_asset *edit_revision_asset;
    char *edit_revision_snapshot;
    char *saved_snapshot;
    int edit_revision_initialized;
    int edit_pending;
    compile_report compile;
    behavior_asset *compiled; /* the linked snapshot compile describes; NULL when unlinked */
    double monotonic_time;
    ed_log_entry log[ED_LOG_MAX];
    int log_count;
    char optimize_summary[192]; /* what the last Optimize changed */
    bool arrange_pending;

    node **selected;
    ed_link_ref *selected_links;
    node *front_node;

    char *pending_snapshot;
    int spawn_link_pending; /* a dragged wire waits for the node search */
    ed_pin_view spawn_link_pin;
    char *clipboard;
    char extract_name[128];
    char connect_error[ED_LOG_MESSAGE];
    const editor_event_signature *event_catalog;
    int event_catalog_count;
    int (*open_asset)(void *, const char *, uint64_t);
    void *open_asset_user;
    const char **prototype_catalog; /* Owned array, borrowed project names. */
    const behavior_asset **asset_catalog; /* Owned array, borrowed project assets. */

    char **undo;
    char **redo;
    char *document_key;
    ed_document_history *documents;
};

/* Private collaboration between document, graph and runtime owners. */
int ed_name_spawned_event(editor *e, node *n);
int ed_event_add_declared_params(editor *e, node *n, const editor_event_signature *signature);
void ed_free_string_stack(char ***stack);
void ed_push_string(char ***stack, char *value);
bool ed_is_graph_call(const node *n);
/* An operator's symbol, such as + or ≤, or NULL for a titled node. */
const char *ed_node_symbol(const node_desc *desc);
const char *ed_node_title(const editor *e, const node *n, char *buf, size_t cap);
const char *ed_pin_type_name(const pin_spec *spec);
int ed_edge_index(const editor *e, const ed_link_ref *link);
const editor_choices *ed_pin_choices(editor *e, node *n, int output, int index);
bool ed_node_has_advanced_inputs(const node *n);
int ed_pin_editable(const graph *g, const node *n, int kind, int pin);
bool ed_pin_visible(const graph *g, const node *n, int kind, int pin);
void ed_pin_label(const node *n, const pin_spec *pin, char *buf, size_t cap);
bool ed_source_scope_available(const editor *e);
const char *ed_binding_label(const editor *e, const pin_binding *binding, char *buf, size_t cap);
const struct ecs_component_desc *ed_component_by_key(const char *key);
bool ed_inline_source_for(const editor *e, const node *target, int index, node **source, int *source_pin);
bool ed_simplify_component_getter(
    editor *e,
    node *source,
    int source_pin,
    node *target,
    int target_pin,
    const node_desc *conversion
);
int ed_optimize_graph(editor *e);
void ed_show_error(editor *e, const char *message);
void ed_clear_connect_error(editor *e);
bool ed_inspection_blocked(const editor *e);
const behavior_instance *ed_display_instance(const editor *e);
const flow_run *ed_display_run(const editor *e);
void ed_refresh_values(editor *e);
void ed_compile(editor *e);
char *ed_snapshot(const editor *e);
void ed_commit_snapshot(editor *e);
void ed_snapshot_before(editor *e);
void ed_cancel_snapshot(editor *e);
void ed_destroy_preview(editor *e);
int ed_adopt_behavior_asset(editor *e, const char *json);
int ed_adopt_behavior_asset_with_identity(editor *e, const char *json, uint64_t owner_identity);
void ed_undo(editor *e);
void ed_redo(editor *e);
void ed_delete_selected(editor *e);
void ed_duplicate_selected(editor *e);
int ed_connection_specs_valid(const pin_spec *output, const pin_spec *input, pin_kind expected_kind);
int ed_connect(editor *e, const ed_pin_view *a, const ed_pin_view *b);
bool ed_insert_on_wire(editor *e, int edge_index, node *n, int in, int out);
void ed_refresh_stale_behavior_expressions(editor *e);
/* Replaces the selection with a call to a new function named e->extract_name;
   returns why it could not, or NULL. */
const char *ed_extract_selection(editor *e);
/* Replaces a pure numeric selection with one Behavior Expression call. */
node *ed_convert_behavior_expression_selection(
    editor *e,
    node *const *selection,
    int count,
    behavior_expression_error *error
);
void ed_clear_selection(editor *e);
void ed_reset_node_interaction(editor *e);
int ed_switch_graph(editor *e, graph *graph);
/* Restores the calls entered to reach the open graph, oldest first. A caller
 * whose call no longer leads on drops out with those before it. */
void ed_restore_callers(editor *e, const ed_graph_caller *callers, int count);
int ed_link_selected(const editor *e, const ed_link_ref *link);
void ed_add_selected_link(editor *e, const ed_link_ref *link);
int ed_node_selected(const editor *e, const node *n);
void ed_add_selected(editor *e, node *n);
void ed_remove_selected(editor *e, node *n);
void ed_select_node(editor *e, node *n, int append);

#endif
