#ifndef CHIBI_EDITOR_INTERNAL_H
#define CHIBI_EDITOR_INTERNAL_H

#include "editor/ui/color.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor.h"
#include "graph/authoring/optimize.h"

#include <imgui/dcimgui.h>

#define ED_HIST_MAX 64
#define ED_LOG_MAX 128
#define ED_LOG_MESSAGE 256

typedef enum ed_pin_kind
{
    ED_PIN_INPUT,
    ED_PIN_OUTPUT
} ed_pin_kind;

typedef struct ed_node_view
{
    node *n;
    uint64_t id;
    ImVec2 min;
    ImVec2 max;
    ImVec2 symbol_min;
    ImVec2 symbol_max;
    float title_height;
    int input_first;
    int output_first;
    int draw_frame;
    editor_node_ui drawn;
} ed_node_view;

typedef struct ed_pin_view
{
    node *n;
    int kind;
    int index;
    ImVec2 pos;
} ed_pin_view;

typedef struct ed_drag_node
{
    node *n;
    float x;
    float y;
} ed_drag_node;

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

typedef struct ed_graph_view
{
    uint64_t graph_id;
    bool fit_mode;
    ImVec2 pan;
    float zoom;
    int view_initialized;
    int entry_index;
} ed_graph_view;

typedef struct ed_graph_caller
{
    ed_graph_view view;
    uint64_t node_id;
} ed_graph_caller;

typedef struct
{
    char *key;
    char *json;
    char *saved;
    uint64_t owner_identity;
    char **undo;
    char **redo;
    ed_graph_view *views;
    editor_location location;
    uint64_t variable_id;
    ed_link_ref *links;
} ed_document_history;

struct editor
{
    behavior_asset *asset;
    editor_ui_context ui_context;
    bool inspector_active;
    int graph_visible;
    char *document_title;
    editor_save_handler save_handler;
    void *save_user;
    graph *active_graph;
    char *math_text;
    char *math_saved;
    size_t math_capacity;
    uint64_t math_node_id;
    behavior_expression_error expression_error;
    ed_graph_view *graph_views;
    ed_graph_caller *graph_callers;
    eval_view *ev;
    editor_value_formatter value_formatter;
    void *value_formatter_user;
    editor_flags_resolver flags_resolver;
    void *flags_user;
    editor_choices_resolver choices_resolver;
    void *choices_user;
    editor_color_resolver color_resolver;
    void *color_user;
    editor_color_state color_state;
    uint64_t open_call_node;
    ImFont *icon_font;
    ImFont *math_font;
    behavior_asset *preview_behavior_asset;
    behavior_instance *instance; /* preview instance, or NULL after an explicit reset */
    const behavior_instance *inspection_instance; /* borrowed runtime target */
    const flow_run *inspection_run; /* borrowed runtime activation */
    bool inspection_blocked;
    const behavior_instance_trace_entry *inspection_trace;
    int inspection_trace_count;
    unsigned instance_generation;
    unsigned edit_revision;
    const behavior_asset *edit_revision_asset;
    char *edit_revision_snapshot;
    char *saved_snapshot;
    int edit_revision_initialized;
    int edit_pending;
    flow_run *activation; /* selected activation the toolbar controls */
    compile_report compile;
    int instance_destroyed; /* the preview was reset or has not been created */
    int entry_index;
    double monotonic_time;
    double execution_pulse_time;
    uint64_t execution_pulse_graph_id;
    int execution_pulse_active;
    ed_log_entry log[ED_LOG_MAX];
    int log_count;
    char optimize_summary[192];
    int optimize_skips[GRAPH_OPTIMIZE_SKIP_COUNT];
    char *log_text;
    size_t log_text_capacity;
    char *compile_text;
    size_t compile_text_capacity;
    double run_started_at;

    ImVec2 canvas_min;
    ImVec2 canvas_size;
    ImVec2 pan;
    float zoom;
    int view_initialized;
    bool arrange_pending;
    bool fit_mode;
    bool fit_resize_pending;
    bool fit_restore_pending;
    double camera_activity_time;
    double canvas_resize_time;
    bool camera_moving;
    ImVec2 camera_pan;
    float camera_zoom;
    node *reveal_target;

    ed_node_view *views;
    ed_pin_view *pins;
    node **selected;
    ed_link_ref *selected_links;

    int selecting;
    int select_additive;
    int panning;
    ImVec2 select_start;
    ImVec2 select_end;

    int dragging;
    int dragging_handle;
    int handle_edge;
    int handle_side;
    int drag_toggle_selected;
    node *drag_toggle_node;
    ImVec2 drag_start_mouse;
    ed_drag_node *drag_nodes;

    int linking;
    ed_pin_view link_pin;
    ImVec2 link_mouse;

    char *pending_snapshot;
    int edit_history_open;
    int edit_changed;
    char edit_text[1024];
    node *edit_text_node;
    int edit_text_kind;
    int edit_text_index;
    char *clipboard;
    char search[64];
    char behavior_asset_search[128];
    int search_selected;
    node *binding_node;
    uint64_t binding_node_id;
    int binding_pin;
    int binding_axis;
    char binding_search[128];
    int binding_selected;
    bool binding_focus_search;
    bool binding_open_requested;
    float popup_x;
    float popup_y;
    node *context_node;
    graph *context_graph;
    char graph_rename[256];
    char extract_name[128];
    char extract_error[256];
    bool extract_requested;
    uint64_t fields_node;
    uint64_t fields_pin;
    bool fields_expand;
    int controls_hovered;
    int has_context_link;
    ed_link_ref context_link;
    node *hovered_node;
    node *front_node;
    ed_link_ref hovered_link;
    int has_hovered_link;
    int spawn_link_pending;
    ed_pin_view spawn_link_pin;
    char connect_error[ED_LOG_MESSAGE];
    ImVec2 connect_error_pos;
    double connect_error_until;
    behavior_variable *selected_variable;
    int details_history_open;
    bool settings_open;
    int settings_frame;
    char details_title[256];
    char details_comment[512];
    const editor_event_signature *event_catalog;
    int event_catalog_count;
    int (*open_asset)(void *, const char *, uint64_t);
    void *open_asset_user;
    const char **prototype_catalog; /* Owned array, borrowed project names. */
    const behavior_asset **asset_catalog; /* Owned array, borrowed project assets. */
    uint64_t field_preview_identity, field_preview_catalog;
    unsigned field_preview_revision;
    float field_values[64 * 64], field_cell_size;
    int field_seed;
    bool field_preview_valid;
    char field_error[256];
    struct app_scene_renderer *shader_preview;
    struct engine_viewport *shader_viewport;
    bool shader_preview_paused;
    uint64_t shader_ramp_node;
    int shader_ramp_stop;
    char shader_image_search[128];
    char shader_image_path[1024];

    char **undo;
    char **redo;
    char *document_key;
    ed_document_history *documents;
};

#define ED_PAD 8.0f
#define ED_TITLE_PAD 6.0f
#define ED_PIN_RADIUS 6.0f
#define ED_GRID_SPACING 32.0f
#define ED_MIN_ZOOM 0.10f
#define ED_READABLE_ZOOM 0.85f
#define ED_MAX_ZOOM 2.5f
#define ED_FIT_MARGIN 32.0f
#define ED_OPEN_TOP_MARGIN 76.0f
#define ED_VALUE_WIDTH 86.0f
#define ED_NODE_CONTROL_WIDTH 240.0f
#define ED_PREVIEW_WIDTH 64.0f
#define ED_TEXT_WIDTH 120.0f
#define ED_NODE_ROW_EXTRA 4.0f
#define ED_LOG_LINE 512
#define ED_TOAST_WIDTH 320.0f
#define ED_TOAST_PAD 10.0f
#define ED_TOAST_ICON 18.0f
#define ED_TOAST_GAP 8.0f
#define ED_TOAST_CLOSE 20.0f
#define ED_TOAST_DURATION 4.0
#define ED_TOAST_FADE 0.2

/* Flow pacing. */
#define ED_FLOW_DOT_SPACING 64.0f
#define ED_FLOW_CURVE_SAMPLES 32

/* Flow timing. */
#define ED_FLOW_ATTACK_HOLD 0.3
#define ED_FLOW_ATTACK_DECAY 0.4
#define ED_FLOW_SUSTAIN_HOLD 0.4
#define ED_FLOW_RELEASE 1.5

/* Flow wire sizing. */
#define ED_FLOW_SELECTED_THICKNESS 4.0f
#define ED_FLOW_HOVERED_THICKNESS 3.0f
#define ED_FLOW_ATTACK_THICKNESS 4.0f
#define ED_FLOW_SUSTAIN_THICKNESS 3.5f
#define ED_FLOW_DATA_SUSTAIN_THICKNESS 2.5f
#define ED_FLOW_RELEASE_THICKNESS 2.5f
#define ED_FLOW_RESTING_EXEC_THICKNESS 2.5f
#define ED_FLOW_RESTING_DATA_THICKNESS 1.5f

/* Flow bubble sizing. */
#define ED_FLOW_DOT_RADIUS 4.0f
#define ED_FLOW_MAX_BUBBLE_RADIUS 5.0f
#define ED_FLOW_BUBBLE_BASE_WEIGHT 1.0f
#define ED_FLOW_BUBBLE_HEAT_WEIGHT 0.65f
#define ED_FLOW_BUBBLE_MIN_LINE_RATIO 0.6f
#define ED_FLOW_BUBBLE_COLOR_BLEND 0.55f

/* Keep overview activity legible without dwarfing its nodes. */
#define ED_FLOW_MIN_ZOOM_SCALE 0.45f
#define ED_FLOW_MAX_ZOOM_SCALE 1.5f

typedef struct
{
    const char *symbol;
    int icon;
} ed_node_presentation;

extern const char ED_ICON_WARNING[], ED_ICON_GRAPH[];
extern const ImVec4 ED_NODE_BODY, ED_NODE_BORDER;
extern const editor_icon ED_GRAPH_OPEN_GLYPH;

/* Private collaboration between document, canvas, inspector and runtime owners. */
int ed_name_spawned_event(editor *e, node *n);
void ed_free_string_stack(char ***stack);
void ed_push_string(char ***stack, char *value);
ImVec4 ed_pin_color(pin_type t);
ImVec4 ed_node_title_color(const node *n);
bool ed_is_graph_call(const node *n);
ed_node_presentation ed_node_appearance(const node_desc *desc);
const char *ed_node_prefix(const node_desc *desc, char icon[4]);
float ed_node_title_inset(const node *n);
float ed_event_status_width(const node *n);
float ed_node_title_padding(const node *n);
float ed_node_body_padding(const node *n);
const char *ed_node_title(const editor *e, const node *n, char *buf, size_t cap);
ImU32 ed_color(ImVec4 c);
ImU32 ed_dim_color(ImU32 c, float factor);
ImVec4 ed_spec_color(const pin_spec *spec);
const char *ed_pin_type_name(const pin_spec *spec);
ImVec2 ed_world_to_screen(const editor *e, float x, float y);
ImVec2 ed_screen_to_world(const editor *e, ImVec2 p);
int ed_edge_index(const editor *e, const ed_link_ref *link);
int ed_pin_rows(const editor *e, const graph *g, const node *n, int kind, int pin);
const editor_flags *ed_pin_flags(const editor *e, const node *n, int output, int index);
const editor_choices *ed_pin_choices(editor *e, node *n, int output, int index);
bool ed_pin_color_input(const editor *e, const node *n, int output, int index);
bool ed_node_has_advanced_inputs(const node *n);
float ed_node_options_height(const node *n);
int ed_pin_editable(const graph *g, const node *n, int kind, int pin);
int ed_vector_width(const pin_spec *pin);
bool ed_pin_visible(const graph *g, const node *n, int kind, int pin);
void ed_pin_label(const node *n, const pin_spec *pin, char *buf, size_t cap);
int ed_field_output(const node *n);
bool ed_fields_expanded(const node *n, int output);
void ed_request_fields(editor *e, node *n, int output);
void ed_draw_fields_menu(editor *e, node *n);
float ed_row_height(const node *n);
float ed_pin_value_width(const graph *g, const node *n, int kind, int pin);
ImVec2 ed_node_dimensions(const editor *e, const graph *g, const node *n);
void ed_build_layout(editor *e);
int ed_point_in_rect(ImVec2 p, ImVec2 min, ImVec2 max);
void ed_select_marquee(editor *e);
ed_pin_view *ed_hit_pin(editor *e, ImVec2 p);
ed_node_view *ed_hit_node(editor *e, ImVec2 p);
ImVec4 ed_lerp4(ImVec4 a, ImVec4 b, float t);
ImVec2 ed_bezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float t);
float ed_distance(ImVec2 a, ImVec2 b);
float ed_curve_length(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3);
ImVec2 ed_curve_at_distance(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, float distance, float length);
float ed_flow_heat(double age, float *thickness, ImVec4 *color, ImVec4 base, int exec);
int ed_link_curve(const editor *e, const edge *ed, ImVec2 *p0, ImVec2 *p1, ImVec2 *p2, ImVec2 *p3);
int ed_hit_handle(const editor *e, ImVec2 mouse, int *edge_index, int *side);
void ed_route_segment(
    const editor *e,
    const edge *ed,
    int segment,
    ImVec2 start,
    ImVec2 end,
    ImVec2 *p0,
    ImVec2 *p1,
    ImVec2 *p2,
    ImVec2 *p3
);
int ed_hit_link_segment(const editor *e, ImVec2 p, ed_link_ref *out, int *route_segment);
int ed_hit_link(const editor *e, ImVec2 p, ed_link_ref *out);
void ed_update_hover(editor *e, ImVec2 mouse);
float ed_font_size(const editor *e);
ImVec2 ed_text_size(const editor *e, const char *text);
void ed_draw_text(const editor *e, ImDrawList *dl, ImVec2 pos, ImU32 color, const char *text);
typedef struct
{
    ImVec2 pos;
    float width;
    const char *label;
} ed_value_layout;
int ed_draw_value_editor(editor *e, node *n, int kind, int index, ed_value_layout layout);
int ed_pin_type_combo(const char *label, pin_type *type);
int ed_pin_container_combo(const char *label, pin_container *container);
void ed_details_begin_mutation(editor *e);
void ed_details_finish_mutation(editor *e, int changed);
void ed_draw_details_pin(editor *e, node *n, int output, int index);
bool ed_binding_scope_available(const editor *e);
bool ed_source_scope_available(const editor *e);
const char *ed_binding_label(const editor *e, const pin_binding *binding, char *buf, size_t cap);
bool ed_inline_source_for(const editor *e, const node *target, int index, node **source, int *source_pin);
int ed_expand_inline_source(editor *e, node *target, int index);
int ed_clear_input_source(editor *e, node *target, int index);
node *ed_vector_source(const editor *e, const node *n, int index);
void ed_open_vector_binding_popup(editor *e, node *n, int index, int axis);
void ed_open_binding_popup(editor *e, node *n, int index);
bool ed_binding_can_convert(const editor *e, const pin_binding *binding);
bool ed_binding_can_jump(const editor *e, const pin_binding *binding);
int ed_convert_binding(editor *e);
void ed_jump_binding_source(editor *e);
int ed_draw_bound_input(editor *e, node *n, int index, ed_value_layout layout);
bool ed_binding_trigger_available(editor *e, node *n, int index);
bool ed_draw_binding_trigger(editor *e, node *n, int index, ImVec2 pos, float size);
void ed_draw_binding_popup(editor *e);
void ed_draw_details_edge(editor *e, const ed_link_ref *ref);
const char *ed_unique_variable_name(const editor *e);
ImVec2 ed_view_center_world(const editor *e);
void ed_show_error(editor *e, const char *message);
void ed_draw_details_behavior_asset(editor *e);
int ed_event_add_declared_params(editor *e, node *n, const editor_event_signature *signature);
int ed_event_unused_payload_count(const editor *e, const node *n);
int ed_repair_event_payloads(editor *e, node *n);
void ed_draw_details(editor *e);
node *ed_place_graph_call_at(editor *e, graph *target, ImVec2 world);
void ed_finish_settings(editor *e);
void ed_draw_canvas(editor *e);
void ed_draw_front_nodes(editor *e);
void ed_draw_selection_overlay(const editor *e);
bool ed_inspection_blocked(const editor *e);
const behavior_instance *ed_display_instance(const editor *e);
const flow_traversal *ed_latest_flow_record(const editor *e, const edge *ed);
void ed_draw_link(editor *e, ImDrawList *dl, const edge *ed);
char *ed_snapshot(const editor *e);
void ed_commit_snapshot(editor *e);
void ed_snapshot_before(editor *e);
void ed_cancel_snapshot(editor *e);
void ed_accept_graph_call_drop(editor *e);
void ed_destroy_preview(editor *e);
int ed_adopt_behavior_asset(editor *e, const char *json);
int ed_adopt_behavior_asset_with_identity(editor *e, const char *json, uint64_t owner_identity);
void ed_undo(editor *e);
void ed_redo(editor *e);
void ed_delete_selected(editor *e);
void ed_copy_selected(editor *e);
void ed_cut_selected(editor *e);
void ed_paste(editor *e);
void ed_duplicate_selected(editor *e);
void ed_begin_drag(editor *e, ImVec2 mouse);
void ed_update_drag(editor *e, ImVec2 mouse);
void ed_update_handle_drag(editor *e, ImVec2 mouse);
int ed_connection_specs_valid(const pin_spec *output, const pin_spec *input, pin_kind expected_kind);
int ed_connect(editor *e, const ed_pin_view *a, const ed_pin_view *b);
int ed_connect_with_inline(editor *e, const ed_pin_view *a, const ed_pin_view *b, bool inline_source);
bool ed_simplify_component_getter(
    editor *e,
    node *source,
    int source_pin,
    node *target,
    int target_pin,
    const node_desc *conversion
);
int ed_optimize_graph(editor *e);
void ed_arrange_in_transaction(editor *e);
void ed_refresh_stale_behavior_expressions(editor *e);
void ed_open_context_menu(editor *e, ImVec2 mouse);
void ed_request_extraction(editor *e);
void ed_draw_extract_popup(editor *e);
void ed_handle_popups(editor *e);
const flow_run *ed_display_run(const editor *e);
void ed_compile(editor *e);
void ed_draw_debug_menu(editor *e);
void ed_rebuild_log_text(editor *e);
void ed_rebuild_compile_text(editor *e);
void ed_auto_arrange(editor *e);
void ed_zoom_fit(editor *e, bool animate);
void ed_frame_initial_view(editor *e);
void ed_apply_field_request(editor *e);
void ed_resize_fitted_view(editor *e, ImVec2 size);
void ed_camera_target(editor *e, ImVec2 pan, float zoom, bool animate);
void ed_camera_advance(editor *e, float dt);
void ed_draw_toolbar(editor *e);
void ed_draw_output_log(editor *e);
void ed_draw_compiler_results(editor *e);
void ed_center_initial_view(editor *e);
int ed_selection_modifier(const ImGuiIO *io);
void ed_handle_input(editor *e);
void ed_handle_shortcuts(editor *e);
bool ed_can_go_back(const editor *e);
ImFont *ed_find_font(const char *name);
void ed_draw_centered_icon(
    ImDrawList *dl,
    ImFont *font,
    float font_size,
    ImVec2 min,
    ImVec2 max,
    ImU32 color,
    ImWchar codepoint
);
int ed_connect_error_rect(const editor *e, ImVec2 *min, ImVec2 *max, ImVec2 *close_min, ImVec2 *close_max);
void ed_clear_connect_error(editor *e);
void ed_draw_connect_error(const editor *e);
int ed_link_equal(const ed_link_ref *a, const ed_link_ref *b);
void ed_clear_selection(editor *e);
void ed_reset_node_interaction(editor *e);
ed_graph_view *ed_graph_view_for(editor *e, uint64_t graph_id, int create);
void ed_save_graph_view(editor *e);
void ed_load_graph_view(editor *e);
int ed_switch_graph(editor *e, graph *graph);
int ed_link_selected(const editor *e, const ed_link_ref *link);
void ed_add_selected_link(editor *e, const ed_link_ref *link);
void ed_remove_selected_link(editor *e, const ed_link_ref *link);
int ed_node_selected(const editor *e, const node *n);
void ed_add_selected(editor *e, node *n);
void ed_remove_selected(editor *e, node *n);
void ed_select_node(editor *e, node *n, int append);
void ed_select_variable(editor *e, behavior_variable *v);

static inline ImVec2 ed_add(ImVec2 a, ImVec2 b)
{
    return (ImVec2){ a.x + b.x, a.y + b.y };
}

static inline ImVec2 ed_sub(ImVec2 a, ImVec2 b)
{
    return (ImVec2){ a.x - b.x, a.y - b.y };
}

static inline ImVec2 ed_scale(ImVec2 a, float s)
{
    return (ImVec2){ a.x * s, a.y * s };
}

static inline float ed_max(float a, float b)
{
    return a > b ? a : b;
}

static inline float ed_min(float a, float b)
{
    return a < b ? a : b;
}

static inline float ed_abs(float a)
{
    return a < 0.0f ? -a : a;
}

#endif
