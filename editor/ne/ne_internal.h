//------------------------------------------------------------------------------
// ne_internal.h — C23 port of imgui_node_editor_internal.h (imgui-node-editor,
// Michal Cichon). Internal state: object pools, settings, interaction state,
// actions, animation.
//
// C++ class hierarchies (Object/Node/Pin/Link, EditorAction/..., Animation/
// ...) become base-struct embedding + a `type` tag. Virtual methods become
// dispatch functions (ne_object_draw, ne_action_accept, ...) that switch on the
// tag, or function pointers where the method set is small. `std::vector<T>` ->
// ckit dynamic array (T* + apush/apop/afree/acount).
//------------------------------------------------------------------------------
#ifndef NE_INTERNAL_H
#define NE_INTERNAL_H

#include "ne.h"
#include "ne_canvas.h"
#include "ne_math.h"
#include "third_party/ckit/ckit.h"

//------------------------------------------------------------------------------
// Object type tag (upstream Detail::ObjectType).
typedef enum ne_object_type
{
    ne_object_type_none = 0,
    ne_object_type_node,
    ne_object_type_link,
    ne_object_type_pin
} ne_object_type;

typedef struct ne_object_id
{
    uintptr_t id;
    int type; /* ne_object_type */
} ne_object_id;

// Object::DrawFlags
enum
{
    ne_draw_flags_none = 0,
    ne_draw_flags_hovered = 1,
    ne_draw_flags_selected = 2,
    ne_draw_flags_highlighted = 4
};

typedef enum ne_node_type
{
    ne_node_type_node,
    ne_node_type_group
} ne_node_type;

typedef enum ne_node_region
{
    ne_node_region_none = 0x00,
    ne_node_region_top = 0x01,
    ne_node_region_bottom = 0x02,
    ne_node_region_left = 0x04,
    ne_node_region_right = 0x08,
    ne_node_region_center = 0x10,
    ne_node_region_header = 0x20,
    ne_node_region_top_left = ne_node_region_top | ne_node_region_left,
    ne_node_region_top_right = ne_node_region_top | ne_node_region_right,
    ne_node_region_bottom_left = ne_node_region_bottom | ne_node_region_left,
    ne_node_region_bottom_right = ne_node_region_bottom | ne_node_region_right
} ne_node_region;

//------------------------------------------------------------------------------
struct ne_editor_context;
struct ne_node;
struct ne_pin;
struct ne_link;

typedef struct ne_object
{
    int type; /* ne_object_type */
    struct ne_editor_context *editor;
    bool m_is_live;
    bool m_is_selected;
    bool m_delete_on_new_frame;
} ne_object;

//------------------------------------------------------------------------------
typedef struct ne_pin
{
    ne_object base;
    ne_pin_id m_id;
    int m_kind; /* ne_pin_kind */
    struct ne_node *m_node;
    ImRect m_bounds;
    ImRect m_pivot;
    struct ne_pin *m_previous_pin;
    ImU32 m_color;
    ImU32 m_border_color;
    float m_border_width;
    float m_rounding;
    int m_corners;
    ImVec2 m_dir;
    float m_strength;
    float m_radius;
    float m_arrow_size;
    float m_arrow_width;
    bool m_snap_link_to_dir;
    bool m_has_connection;
    bool m_had_connection;
} ne_pin;

typedef struct ne_node
{
    ne_object base;
    ne_node_id m_id;
    int m_type; /* ne_node_type */
    ImRect m_bounds;
    float m_z_position;
    int m_channel;
    struct ne_pin *m_last_pin;
    ImVec2 m_drag_start;

    ImU32 m_color;
    ImU32 m_border_color;
    float m_border_width;
    float m_rounding;

    ImU32 m_group_color;
    ImU32 m_group_border_color;
    float m_group_border_width;
    float m_group_rounding;
    ImRect m_group_bounds;

    bool m_highlight_connected_links;

    bool m_restore_state;
    bool m_center_on_screen;
} ne_node;

struct ne_link
{
    ne_object base;
    ne_link_id m_id;
    struct ne_pin *m_start_pin;
    struct ne_pin *m_end_pin;
    ImU32 m_color;
    ImU32 m_highlight_color;
    float m_thickness;
    ImVec2 m_start;
    ImVec2 m_end;
};

// Object dispatch helpers (virtual method equivalents).
ne_object_id ne_object_id_of(ne_object *object);
bool ne_object_is_visible(ne_object *object);
void ne_object_reset(ne_object *object);
void ne_object_draw(ne_object *object, ImDrawList *draw_list, int flags);
bool ne_object_accept_drag(ne_object *object);
void ne_object_update_drag(ne_object *object, ImVec2 offset);
bool ne_object_end_drag(ne_object *object);
ImVec2 ne_object_drag_start_location(ne_object *object);
bool ne_object_is_draggable(ne_object *object);
bool ne_object_is_selectable(ne_object *object);
bool ne_object_test_hit(ne_object *object, ImVec2 point, float extra_thickness);
bool ne_object_test_hit_rect(ne_object *object, ImRect rect, bool allow_intersect);
ImRect ne_object_get_bounds(ne_object *object);
ne_node *ne_object_as_node(ne_object *object);
ne_pin *ne_object_as_pin(ne_object *object);
struct ne_link *ne_object_as_link(ne_object *object);

// Concrete object methods (ne_* prefix kept mechanical).
void ne_pin_draw(ne_pin *pin, ImDrawList *draw_list, int flags);
ImVec2 ne_pin_get_closest_point(const ne_pin *pin, ImVec2 p);
ne_line ne_pin_get_closest_line(const ne_pin *pin, const ne_pin *other);

bool ne_node_accept_drag(ne_node *node);
void ne_node_update_drag(ne_node *node, ImVec2 offset);
bool ne_node_end_drag(ne_node *node);
void ne_node_draw(ne_node *node, ImDrawList *draw_list, int flags);
void ne_node_draw_border(ne_node *node, ImDrawList *draw_list, ImU32 color, float thickness, float offset);
void ne_node_get_grouped_nodes(ne_node *node, ne_node ***result, bool append);
ImRect ne_node_get_region_bounds(const ne_node *node, int region);
int ne_node_get_region(const ne_node *node, ImVec2 point);

void ne_link_draw(struct ne_link *link, ImDrawList *draw_list, int flags);
void ne_link_draw_color(struct ne_link *link, ImDrawList *draw_list, ImU32 color, float extra_thickness);
void ne_link_update_endpoints(struct ne_link *link);
struct ne_cubic_bezier_points ne_link_get_curve(const struct ne_link *link);
bool ne_link_test_hit(struct ne_link *link, ImVec2 point, float extra_thickness);
bool ne_link_test_hit_rect(struct ne_link *link, ImRect rect, bool allow_intersect);
ImRect ne_link_get_bounds(const struct ne_link *link);

//------------------------------------------------------------------------------
typedef struct ne_node_settings
{
    ne_node_id m_id;
    ImVec2 m_location;
    ImVec2 m_size;
    ImVec2 m_group_size;
    bool m_was_used;

    bool m_saved;
    bool m_is_dirty;
    ne_save_reason_flags m_dirty_reason;
} ne_node_settings;

typedef struct ne_settings
{
    bool m_is_dirty;
    ne_save_reason_flags m_dirty_reason;

    ne_node_settings *m_nodes; /* ckit array */
    ne_object_id *m_selection; /* ckit array */
    ImVec2 m_view_scroll;
    float m_view_zoom;
    ImRect m_visible_rect;
} ne_settings;

ne_node_settings *ne_settings_add_node(ne_settings *settings, ne_node_id id);
ne_node_settings *ne_settings_find_node(ne_settings *settings, ne_node_id id);
void ne_settings_remove_node(ne_settings *settings, ne_node_id id);
void ne_settings_clear_dirty(ne_settings *settings, ne_node *node);
void ne_settings_make_dirty(ne_settings *settings, ne_save_reason_flags reason, ne_node *node);

void ne_node_settings_clear_dirty(ne_node_settings *settings);
void ne_node_settings_make_dirty(ne_node_settings *settings, ne_save_reason_flags reason);

//------------------------------------------------------------------------------
typedef struct ne_control
{
    ne_object *hot_object;
    ne_object *active_object;
    ne_object *clicked_object;
    ne_object *double_clicked_object;
    ne_node *hot_node;
    ne_node *active_node;
    ne_node *clicked_node;
    ne_node *double_clicked_node;
    ne_pin *hot_pin;
    ne_pin *active_pin;
    ne_pin *clicked_pin;
    ne_pin *double_clicked_pin;
    struct ne_link *hot_link;
    struct ne_link *active_link;
    struct ne_link *clicked_link;
    struct ne_link *double_clicked_link;
    bool background_hot;
    bool background_active;
    int background_click_button_index;
    int background_double_click_button_index;
} ne_control;

ne_control ne_control_make(
    ne_object *hot_object,
    ne_object *active_object,
    ne_object *clicked_object,
    ne_object *double_clicked_object,
    bool background_hot,
    bool background_active,
    int background_click_button_index,
    int background_double_click_button_index
);

//------------------------------------------------------------------------------
// Animation.
typedef enum ne_animation_state
{
    ne_animation_playing,
    ne_animation_stopped
} ne_animation_state;

typedef struct ne_animation
{
    int state; /* ne_animation_state */
    struct ne_editor_context *editor;
    float m_time;
    float m_duration;

    void (*on_play)(struct ne_animation *animation);
    void (*on_finish)(struct ne_animation *animation);
    void (*on_stop)(struct ne_animation *animation);
    void (*on_update)(struct ne_animation *animation, float progress);
} ne_animation;

void ne_animation_play(ne_animation *animation, float duration);
void ne_animation_stop(ne_animation *animation);
void ne_animation_finish(ne_animation *animation);
void ne_animation_update(ne_animation *animation);
bool ne_animation_is_playing(const ne_animation *animation);
float ne_animation_get_progress(const ne_animation *animation);

typedef struct ne_animation_controller
{
    struct ne_editor_context *editor;
    void (*draw)(struct ne_animation_controller *controller, ImDrawList *draw_list);
} ne_animation_controller;

typedef struct ne_curve_point
{
    float distance;
    ImVec2 point;
} ne_curve_point;

struct ne_flow_animation;
struct ne_flow_animation_controller;

typedef struct ne_navigate_animation
{
    ne_animation base;
    struct ne_navigate_action *action;
    ImRect m_start;
    ImRect m_target;
} ne_navigate_animation;

typedef struct ne_flow_animation
{
    ne_animation base;
    struct ne_flow_animation_controller *controller;
    struct ne_link *m_link;
    float m_speed;
    float m_marker_distance;
    float m_offset;
    ImVec2 m_last_start;
    ImVec2 m_last_end;
    float m_path_length;
    ne_curve_point *m_path; /* ckit array */
} ne_flow_animation;

typedef struct ne_flow_animation_controller
{
    ne_animation_controller base;
    ne_flow_animation **m_animations; /* ckit array */
    ne_flow_animation **m_free_pool;  /* ckit array */
} ne_flow_animation_controller;

//------------------------------------------------------------------------------
// Actions.
typedef enum ne_action_type
{
    ne_action_type_navigate = 0,
    ne_action_type_size,
    ne_action_type_drag,
    ne_action_type_select,
    ne_action_type_context_menu,
    ne_action_type_shortcut,
    ne_action_type_create_item,
    ne_action_type_delete_items
} ne_action_type;

typedef enum ne_accept_result
{
    ne_accept_false = 0,
    ne_accept_true = 1,
    ne_accept_possible = 2
} ne_accept_result;

typedef enum ne_zoom_mode
{
    ne_zoom_mode_none,
    ne_zoom_mode_exact,
    ne_zoom_mode_with_margin
} ne_zoom_mode;

typedef enum ne_navigation_reason
{
    ne_navigation_reason_unknown,
    ne_navigation_reason_mouse_zoom,
    ne_navigation_reason_selection,
    ne_navigation_reason_object,
    ne_navigation_reason_content,
    ne_navigation_reason_edge
} ne_navigation_reason;

typedef struct ne_editor_action
{
    int type; /* ne_action_type */
    struct ne_editor_context *editor;

    const char *(*get_name)(struct ne_editor_action *action);
    ne_accept_result (*accept)(struct ne_editor_action *action, const ne_control *control);
    bool (*process)(struct ne_editor_action *action, const ne_control *control);
    void (*reject)(struct ne_editor_action *action);
    int (*get_cursor)(struct ne_editor_action *action); /* ImGuiMouseCursor */
    bool (*is_dragging)(struct ne_editor_action *action);
    void (*show_metrics)(struct ne_editor_action *action);
} ne_editor_action;

// Action dispatch (virtual method equivalents + As* casts).
const char *ne_action_get_name(ne_editor_action *action);
ne_accept_result ne_action_accept(ne_editor_action *action, const ne_control *control);
bool ne_action_process(ne_editor_action *action, const ne_control *control);
void ne_action_reject(ne_editor_action *action);
int ne_action_get_cursor(ne_editor_action *action);
bool ne_action_is_dragging(ne_editor_action *action);
void ne_action_show_metrics(ne_editor_action *action);

struct ne_navigate_action *ne_action_as_navigate(ne_editor_action *action);
struct ne_size_action *ne_action_as_size(ne_editor_action *action);
struct ne_drag_action *ne_action_as_drag(ne_editor_action *action);
struct ne_select_action *ne_action_as_select(ne_editor_action *action);
struct ne_create_item_action *ne_action_as_create_item(ne_editor_action *action);
struct ne_delete_items_action *ne_action_as_delete_items(ne_editor_action *action);
struct ne_context_menu_action *ne_action_as_context_menu(ne_editor_action *action);
struct ne_shortcut_action *ne_action_as_shortcut(ne_editor_action *action);

//------------------------------------------------------------------------------
typedef struct ne_navigate_action
{
    ne_editor_action base;
    bool m_is_active;
    float m_zoom;
    ImRect m_visible_rect;
    ImVec2 m_scroll;
    ImVec2 m_scroll_start;
    ImVec2 m_scroll_delta;
    ne_canvas *m_canvas;
    ImVec2 m_window_screen_pos;
    ImVec2 m_window_screen_size;
    /* NavigateAnimation m_animation */
    ne_navigate_animation m_animation;
    int m_reason; /* ne_navigation_reason */
    uint64_t m_last_selection_id;
    ne_object *m_last_object;
    bool m_moving_over_edge;
    ImVec2 m_move_screen_offset;
    const float *m_zoom_levels;
    int m_zoom_level_count;
} ne_navigate_action;

typedef struct ne_size_action
{
    ne_editor_action base;
    bool m_is_active;
    bool m_clean;
    ne_node *m_sized_node;
    ImRect m_start_bounds;
    ImRect m_start_group_bounds;
    ImVec2 m_last_size;
    ImVec2 m_minimum_size;
    ImVec2 m_last_drag_offset;
    int m_pivot;  /* ne_node_region */
    int m_cursor; /* ImGuiMouseCursor */
} ne_size_action;

typedef struct ne_drag_action
{
    ne_editor_action base;
    bool m_is_active;
    bool m_clear;
    ne_object *m_dragged_object;
    ne_object **m_objects; /* ckit array */
} ne_drag_action;

typedef struct ne_select_action
{
    ne_editor_action base;
    bool m_is_active;
    bool m_select_groups;
    bool m_select_link_mode;
    bool m_commit_selection;
    ImVec2 m_start_point;
    ImVec2 m_end_point;
    ne_object **m_candidate_objects;         /* ckit array */
    ne_object **m_selected_objects_at_start; /* ckit array */
    ne_animation m_animation;
} ne_select_action;

typedef struct ne_context_menu_action
{
    ne_editor_action base;
    int m_candidate_menu; /* ne_context_menu */
    int m_current_menu;
    ne_object_id m_context_id;
} ne_context_menu_action;

enum
{
    ne_context_menu_none,
    ne_context_menu_node,
    ne_context_menu_pin,
    ne_context_menu_link,
    ne_context_menu_background
};

typedef struct ne_shortcut_action
{
    ne_editor_action base;
    bool m_is_active;
    bool m_in_action;
    int m_current_action;  /* ne_shortcut_action_type */
    ne_object **m_context; /* ckit array */
} ne_shortcut_action;

enum
{
    ne_shortcut_action_none,
    ne_shortcut_action_cut,
    ne_shortcut_action_copy,
    ne_shortcut_action_paste,
    ne_shortcut_action_duplicate,
    ne_shortcut_action_create_node
};

typedef struct ne_create_item_action
{
    ne_editor_action base;
    bool m_in_active;
    int m_next_stage; /* ne_create_stage */
    int m_current_stage;
    int m_item_type;   /* ne_create_item_type */
    int m_user_action; /* ne_create_action_type */
    ImU32 m_link_color;
    float m_link_thickness;
    ne_pin *m_link_start;
    ne_pin *m_link_end;
    bool m_is_active;
    ne_pin *m_dragged_pin;
    int m_last_channel;
    bool m_is_in_global_space;
} ne_create_item_action;

enum
{
    ne_create_stage_none,
    ne_create_stage_possible,
    ne_create_stage_create
};
enum
{
    ne_create_action_unknown,
    ne_create_action_user_reject,
    ne_create_action_user_accept
};
enum
{
    ne_create_item_none,
    ne_create_item_node,
    ne_create_item_link
};
enum
{
    ne_create_result_true,
    ne_create_result_false,
    ne_create_result_indeterminate
};

typedef struct ne_delete_items_action
{
    ne_editor_action base;
    bool m_is_active;
    bool m_in_interaction;
    ne_object **m_manually_deleted_objects; /* ckit array */
    int m_current_item_type;                /* ne_delete_iterator_type */
    int m_user_action;                      /* ne_delete_user_action */
    ne_object **m_candidate_objects;        /* ckit array */
    int m_candidate_item_index;
} ne_delete_items_action;

enum
{
    ne_delete_iterator_unknown,
    ne_delete_iterator_link,
    ne_delete_iterator_node
};
enum
{
    ne_delete_user_undetermined,
    ne_delete_user_accepted,
    ne_delete_user_rejected
};

//------------------------------------------------------------------------------
typedef struct ne_node_builder
{
    ne_editor_context *editor;
    ne_node *m_current_node;
    ne_pin *m_current_pin;
    ImRect m_node_rect;
    ImRect m_pivot_rect;
    ImVec2 m_pivot_alignment;
    ImVec2 m_pivot_size;
    ImVec2 m_pivot_scale;
    bool m_resolve_pin_rect;
    bool m_resolve_pivot;
    ImRect m_group_bounds;
    bool m_is_group;
    ImDrawListSplitter m_splitter;
    ImDrawListSplitter m_pin_splitter;
} ne_node_builder;

typedef struct ne_hint_builder
{
    ne_editor_context *editor;
    bool m_is_active;
    ne_node *m_current_node;
    float m_last_fringe;
    int m_last_channel;
} ne_hint_builder;

//------------------------------------------------------------------------------
// Style with modifier stacks (upstream Detail::Style).
typedef struct ne_style_color_modifier
{
    int index; /* ne_style_color */
    ImVec4 value;
} ne_style_color_modifier;

typedef struct ne_style_var_modifier
{
    int index; /* ne_style_var */
    ImVec4 value;
} ne_style_var_modifier;

typedef struct ne_style_ctx
{
    ne_style base;
    ne_style_color_modifier *color_stack; /* ckit array */
    ne_style_var_modifier *var_stack;     /* ckit array */
} ne_style_ctx;

//------------------------------------------------------------------------------
// Editor context.
typedef enum ne_suspend_flags
{
    ne_suspend_flags_none = 0,
    ne_suspend_flags_keep_splitter = 1
} ne_suspend_flags;

struct ne_editor_context
{
    ne_config m_config;

    ImGuiID m_editor_active_id;
    bool m_is_first_frame;
    bool m_is_focused;
    bool m_is_hovered;
    bool m_is_hovered_without_overlapp;

    bool m_shortcuts_enabled;

    ne_style_ctx m_style;

    ne_node **m_nodes;        /* ckit array */
    ne_pin **m_pins;          /* ckit array */
    struct ne_link **m_links; /* ckit array */

    ne_object **m_selected_objects; /* ckit array */

    ne_object **m_last_selected_objects; /* ckit array */
    uint64_t m_selection_id;

    struct ne_link *m_last_active_link;

    ne_animation **m_live_animations;      /* ckit array */
    ne_animation **m_last_live_animations; /* ckit array */

    ne_canvas m_canvas;
    bool m_is_canvas_visible;

    ne_node_builder m_node_builder;
    ne_hint_builder m_hint_builder;

    ne_editor_action *m_current_action;
    ne_navigate_action m_navigate_action;
    ne_size_action m_size_action;
    ne_drag_action m_drag_action;
    ne_select_action m_select_action;
    ne_context_menu_action m_context_menu_action;
    ne_shortcut_action m_shortcut_action;
    ne_create_item_action m_create_item_action;
    ne_delete_items_action m_delete_items_action;

    ne_animation_controller **m_animation_controllers; /* ckit array */
    ne_flow_animation_controller m_flow_animation_controller; /* FlowAnimationController (embedded) */

    ne_node_id m_hovered_node;
    ne_pin_id m_hovered_pin;
    ne_link_id m_hovered_link;
    ne_node_id m_double_clicked_node;
    ne_pin_id m_double_clicked_pin;
    ne_link_id m_double_clicked_link;
    int m_background_click_button_index;
    int m_background_double_click_button_index;

    bool m_is_initialized;
    ne_settings m_settings;

    ImDrawList *m_draw_list;
    int m_external_channel;
    ImDrawListSplitter m_splitter;
};

// Editor context methods (ne_* prefix).
void ne_editor_context_init(ne_editor_context *editor, const ne_config *config);
void ne_editor_context_free(ne_editor_context *editor);
void ne_editor_context_begin(ne_editor_context *editor, const char *id, ImVec2 size);
void ne_editor_context_end(ne_editor_context *editor);
bool ne_editor_context_do_link(
    ne_editor_context *editor,
    ne_link_id id,
    ne_pin_id start_pin_id,
    ne_pin_id end_pin_id,
    ImU32 color,
    float thickness
);

void ne_editor_context_set_node_position(ne_editor_context *editor, ne_node_id node_id, ImVec2 position);
void ne_editor_context_set_group_size(ne_editor_context *editor, ne_node_id node_id, ImVec2 size);
ImVec2 ne_editor_context_get_node_position(ne_editor_context *editor, ne_node_id node_id);
ImVec2 ne_editor_context_get_node_size(ne_editor_context *editor, ne_node_id node_id);
void ne_editor_context_set_node_z_position(ne_editor_context *editor, ne_node_id node_id, float z);
float ne_editor_context_get_node_z_position(ne_editor_context *editor, ne_node_id node_id);
void ne_editor_context_mark_node_to_restore_state(ne_editor_context *editor, ne_node *node);
void ne_editor_context_update_node_state(ne_editor_context *editor, ne_node *node);
void ne_editor_context_remove_settings(ne_editor_context *editor, ne_object *object);

void ne_editor_context_clear_selection(ne_editor_context *editor);
void ne_editor_context_select_object(ne_editor_context *editor, ne_object *object);
void ne_editor_context_deselect_object(ne_editor_context *editor, ne_object *object);
void ne_editor_context_set_selected_object(ne_editor_context *editor, ne_object *object);
void ne_editor_context_toggle_object_selection(ne_editor_context *editor, ne_object *object);
bool ne_editor_context_is_selected(ne_editor_context *editor, ne_object *object);
bool ne_editor_context_is_any_node_selected(ne_editor_context *editor);
bool ne_editor_context_is_any_link_selected(ne_editor_context *editor);
bool ne_editor_context_has_selection_changed(ne_editor_context *editor);

ne_node *ne_editor_context_find_node_at(ne_editor_context *editor, ImVec2 p);
void ne_editor_context_find_nodes_in_rect(
    ne_editor_context *editor,
    ImRect r,
    ne_node ***result,
    bool append,
    bool include_intersecting
);
void ne_editor_context_find_links_in_rect(
    ne_editor_context *editor,
    ImRect r,
    struct ne_link ***result,
    bool append
);

bool ne_editor_context_has_any_links_node(ne_editor_context *editor, ne_node_id node_id);
bool ne_editor_context_has_any_links_pin(ne_editor_context *editor, ne_pin_id pin_id);
int ne_editor_context_break_links_node(ne_editor_context *editor, ne_node_id node_id);
int ne_editor_context_break_links_pin(ne_editor_context *editor, ne_pin_id pin_id);
void ne_editor_context_find_links_for_node(
    ne_editor_context *editor,
    ne_node_id node_id,
    struct ne_link ***result,
    bool add
);

bool ne_editor_context_pin_had_any_links(ne_editor_context *editor, ne_pin_id pin_id);
ImVec2 ne_editor_context_to_canvas(ne_editor_context *editor, ImVec2 point);
ImVec2 ne_editor_context_to_screen(ne_editor_context *editor, ImVec2 point);
void ne_editor_context_notify_link_deleted(ne_editor_context *editor, struct ne_link *link);

void ne_editor_context_suspend(ne_editor_context *editor, ne_suspend_flags flags);
void ne_editor_context_resume(ne_editor_context *editor, ne_suspend_flags flags);
bool ne_editor_context_is_suspended(ne_editor_context *editor);
bool ne_editor_context_is_focused(ne_editor_context *editor);
bool ne_editor_context_is_hovered(ne_editor_context *editor);
bool ne_editor_context_is_hovered_without_overlapp(ne_editor_context *editor);
bool ne_editor_context_can_accept_user_input(ne_editor_context *editor);

void ne_editor_context_make_dirty(ne_editor_context *editor, ne_save_reason_flags reason);
void ne_editor_context_make_dirty_node(
    ne_editor_context *editor,
    ne_save_reason_flags reason,
    ne_node *node
);

int ne_editor_context_count_live_nodes(ne_editor_context *editor);
int ne_editor_context_count_live_pins(ne_editor_context *editor);
int ne_editor_context_count_live_links(ne_editor_context *editor);

ne_pin *ne_editor_context_create_pin(ne_editor_context *editor, ne_pin_id id, ne_pin_kind kind);
ne_node *ne_editor_context_create_node(ne_editor_context *editor, ne_node_id id);
struct ne_link *ne_editor_context_create_link(ne_editor_context *editor, ne_link_id id);

ne_node *ne_editor_context_find_node(ne_editor_context *editor, ne_node_id id);
ne_pin *ne_editor_context_find_pin(ne_editor_context *editor, ne_pin_id id);
struct ne_link *ne_editor_context_find_link(ne_editor_context *editor, ne_link_id id);
ne_object *ne_editor_context_find_object(ne_editor_context *editor, ne_object_id id);

ne_node *ne_editor_context_get_node(ne_editor_context *editor, ne_node_id id);
ne_pin *ne_editor_context_get_pin(ne_editor_context *editor, ne_pin_id id, ne_pin_kind kind);
struct ne_link *ne_editor_context_get_link(ne_editor_context *editor, ne_link_id id);

struct ne_link *ne_editor_context_find_link_at(ne_editor_context *editor, ImVec2 p);

ImRect ne_editor_context_get_selection_bounds(ne_editor_context *editor);
ImRect ne_editor_context_get_content_bounds(ne_editor_context *editor);

ImU32 ne_editor_context_get_color(ne_editor_context *editor, ne_style_color color_index);
ImU32 ne_editor_context_get_color_alpha(ne_editor_context *editor, ne_style_color color_index, float alpha);

int ne_editor_context_get_node_ids(ne_editor_context *editor, ne_node_id *nodes, int size);

void ne_editor_context_navigate_to(ne_editor_context *editor, ImRect bounds, bool zoom_in, float duration);

void ne_editor_context_register_animation(ne_editor_context *editor, ne_animation *animation);
void ne_editor_context_unregister_animation(ne_editor_context *editor, ne_animation *animation);

void ne_editor_context_flow(ne_editor_context *editor, struct ne_link *link, ne_flow_direction direction);

void ne_editor_context_set_user_context(ne_editor_context *editor, bool global_space);

void ne_editor_context_enable_shortcuts(ne_editor_context *editor, bool enable);
bool ne_editor_context_are_shortcuts_enabled(ne_editor_context *editor);

float ne_editor_context_align_point_to_grid(ne_editor_context *editor, float p);
ImVec2 ne_editor_context_align_point_to_grid_v2(ne_editor_context *editor, ImVec2 p);

ImDrawList *ne_editor_context_get_draw_list(ne_editor_context *editor);

// Action init functions.
void ne_navigate_action_init(ne_navigate_action *action, ne_editor_context *editor, ne_canvas *canvas);
void ne_size_action_init(ne_size_action *action, ne_editor_context *editor);
void ne_drag_action_init(ne_drag_action *action, ne_editor_context *editor);
void ne_select_action_init(ne_select_action *action, ne_editor_context *editor);
void ne_context_menu_action_init(ne_context_menu_action *action, ne_editor_context *editor);
void ne_shortcut_action_init(ne_shortcut_action *action, ne_editor_context *editor);
void ne_create_item_action_init(ne_create_item_action *action, ne_editor_context *editor);
void ne_delete_items_action_init(ne_delete_items_action *action, ne_editor_context *editor);

void ne_node_builder_init(ne_node_builder *builder, ne_editor_context *editor);
void ne_hint_builder_init(ne_hint_builder *builder, ne_editor_context *editor);
void ne_style_ctx_init(ne_style_ctx *style);

// Action method functions (called directly and through ne_action_* dispatch).
ne_accept_result ne_navigate_action_accept(ne_navigate_action *action, const ne_control *control);
bool ne_navigate_action_process(ne_navigate_action *action, const ne_control *control);
void ne_navigate_action_show_metrics(ne_navigate_action *action);
void ne_navigate_action_set_window(ne_navigate_action *action, ImVec2 position, ImVec2 size);
ne_canvas_view ne_navigate_action_get_view(const ne_navigate_action *action);
ImRect ne_navigate_action_get_view_rect(const ne_navigate_action *action);
void ne_navigate_action_set_view_rect(ne_navigate_action *action, ImRect rect);
void ne_navigate_action_navigate_to(
    ne_navigate_action *action,
    ImRect bounds,
    ne_zoom_mode zoom_mode,
    float duration,
    ne_navigation_reason reason
);
void ne_navigate_action_stop_navigation(ne_navigate_action *action);
void ne_navigate_action_finish_navigation(ne_navigate_action *action);
bool ne_navigate_action_move_over_edge(ne_navigate_action *action, ImVec2 canvas_size);
void ne_navigate_action_stop_move_over_edge(ne_navigate_action *action);
ImVec2 ne_navigate_action_get_move_screen_offset(const ne_navigate_action *action);
bool ne_navigate_action_is_moving_over_edge(const ne_navigate_action *action);

ne_accept_result ne_size_action_accept(ne_size_action *action, const ne_control *control);
bool ne_size_action_process(ne_size_action *action, const ne_control *control);
void ne_size_action_show_metrics(ne_size_action *action);

ne_accept_result ne_drag_action_accept(ne_drag_action *action, const ne_control *control);
bool ne_drag_action_process(ne_drag_action *action, const ne_control *control);
void ne_drag_action_show_metrics(ne_drag_action *action);

ne_accept_result ne_select_action_accept(ne_select_action *action, const ne_control *control);
bool ne_select_action_process(ne_select_action *action, const ne_control *control);
void ne_select_action_show_metrics(ne_select_action *action);
void ne_select_action_draw(ne_select_action *action, ImDrawList *draw_list);

ne_accept_result ne_context_menu_action_accept(ne_context_menu_action *action, const ne_control *control);
bool ne_context_menu_action_process(ne_context_menu_action *action, const ne_control *control);
void ne_context_menu_action_reject(ne_context_menu_action *action);
void ne_context_menu_action_show_metrics(ne_context_menu_action *action);
bool ne_context_menu_action_show_node(ne_context_menu_action *action, ne_node_id *node_id);
bool ne_context_menu_action_show_pin(ne_context_menu_action *action, ne_pin_id *pin_id);
bool ne_context_menu_action_show_link(ne_context_menu_action *action, ne_link_id *link_id);
bool ne_context_menu_action_show_background(ne_context_menu_action *action);

ne_accept_result ne_shortcut_action_accept(ne_shortcut_action *action, const ne_control *control);
bool ne_shortcut_action_process(ne_shortcut_action *action, const ne_control *control);
void ne_shortcut_action_reject(ne_shortcut_action *action);
void ne_shortcut_action_show_metrics(ne_shortcut_action *action);
bool ne_shortcut_action_begin(ne_shortcut_action *action);
void ne_shortcut_action_end(ne_shortcut_action *action);
bool ne_shortcut_action_accept_cut(ne_shortcut_action *action);
bool ne_shortcut_action_accept_copy(ne_shortcut_action *action);
bool ne_shortcut_action_accept_paste(ne_shortcut_action *action);
bool ne_shortcut_action_accept_duplicate(ne_shortcut_action *action);
bool ne_shortcut_action_accept_create_node(ne_shortcut_action *action);

ne_accept_result ne_create_item_action_accept(ne_create_item_action *action, const ne_control *control);
bool ne_create_item_action_process(ne_create_item_action *action, const ne_control *control);
void ne_create_item_action_show_metrics(ne_create_item_action *action);
void ne_create_item_action_set_style(ne_create_item_action *action, ImU32 color, float thickness);
bool ne_create_item_action_begin(ne_create_item_action *action);
void ne_create_item_action_end(ne_create_item_action *action);
int ne_create_item_action_reject_item(ne_create_item_action *action);
int ne_create_item_action_accept_item(ne_create_item_action *action);
int ne_create_item_action_query_link(ne_create_item_action *action, ne_pin_id *start_id, ne_pin_id *end_id);
int ne_create_item_action_query_node(ne_create_item_action *action, ne_pin_id *pin_id);

ne_accept_result ne_delete_items_action_accept(ne_delete_items_action *action, const ne_control *control);
bool ne_delete_items_action_process(ne_delete_items_action *action, const ne_control *control);
void ne_delete_items_action_show_metrics(ne_delete_items_action *action);
bool ne_delete_items_action_add(ne_delete_items_action *action, ne_object *object);
bool ne_delete_items_action_begin(ne_delete_items_action *action);
void ne_delete_items_action_end(ne_delete_items_action *action);
bool ne_delete_items_action_query_link(
    ne_delete_items_action *action,
    ne_link_id *link_id,
    ne_pin_id *start_id,
    ne_pin_id *end_id
);
bool ne_delete_items_action_query_node(ne_delete_items_action *action, ne_node_id *node_id);
bool ne_delete_items_action_accept_item(ne_delete_items_action *action, bool delete_dependencies);
void ne_delete_items_action_reject_item(ne_delete_items_action *action);

// Animation.
void ne_navigate_animation_init(
    ne_navigate_animation *animation,
    ne_editor_context *editor,
    ne_navigate_action *action
);
void ne_flow_animation_init(ne_flow_animation *animation, ne_flow_animation_controller *controller);
void ne_flow_animation_flow(
    ne_flow_animation *animation,
    struct ne_link *link,
    float marker_distance,
    float speed,
    float duration
);
void ne_flow_animation_draw(ne_flow_animation *animation, ImDrawList *draw_list);
void ne_flow_animation_controller_init(ne_flow_animation_controller *controller, ne_editor_context *editor);
void ne_flow_animation_controller_free(ne_flow_animation_controller *controller);
void ne_flow_animation_controller_flow(
    ne_flow_animation_controller *controller,
    struct ne_link *link,
    ne_flow_direction direction
);
void ne_flow_animation_controller_draw(ne_flow_animation_controller *controller, ImDrawList *draw_list);

// Node builder / hint builder methods.
void ne_node_builder_begin(ne_node_builder *builder, ne_node_id node_id);
void ne_node_builder_end(ne_node_builder *builder);
void ne_node_builder_begin_pin(ne_node_builder *builder, ne_pin_id pin_id, ne_pin_kind kind);
void ne_node_builder_end_pin(ne_node_builder *builder);
void ne_node_builder_pin_rect(ne_node_builder *builder, ImVec2 a, ImVec2 b);
void ne_node_builder_pin_pivot_rect(ne_node_builder *builder, ImVec2 a, ImVec2 b);
void ne_node_builder_pin_pivot_size(ne_node_builder *builder, ImVec2 size);
void ne_node_builder_pin_pivot_scale(ne_node_builder *builder, ImVec2 scale);
void ne_node_builder_pin_pivot_alignment(ne_node_builder *builder, ImVec2 alignment);
void ne_node_builder_group(ne_node_builder *builder, ImVec2 size);
ImDrawList *ne_node_builder_get_user_background_draw_list(ne_node_builder *builder);
ImDrawList *ne_node_builder_get_user_background_draw_list_node(ne_node_builder *builder, ne_node *node);

bool ne_hint_builder_begin(ne_hint_builder *builder, ne_node_id node_id);
void ne_hint_builder_end(ne_hint_builder *builder);
ImVec2 ne_hint_builder_get_group_min(ne_hint_builder *builder);
ImVec2 ne_hint_builder_get_group_max(ne_hint_builder *builder);
ImDrawList *ne_hint_builder_get_foreground_draw_list(ne_hint_builder *builder);
ImDrawList *ne_hint_builder_get_background_draw_list(ne_hint_builder *builder);

// Style.
void ne_style_ctx_push_color(ne_style_ctx *style, ne_style_color color_index, ImVec4 color);
void ne_style_ctx_pop_color(ne_style_ctx *style, int count);
void ne_style_ctx_push_var_f(ne_style_ctx *style, ne_style_var var_index, float value);
void ne_style_ctx_push_var_v2(ne_style_ctx *style, ne_style_var var_index, ImVec2 value);
void ne_style_ctx_push_var_v4(ne_style_ctx *style, ne_style_var var_index, ImVec4 value);
void ne_style_ctx_pop_var(ne_style_ctx *style, int count);
const char *ne_style_ctx_get_color_name(const ne_style_ctx *style, ne_style_color color_index);

// Config (I/O + callbacks).
char *ne_config_load(ne_config *config);
char *ne_config_load_node(ne_config *config, ne_node_id node_id);
void ne_config_begin_save(ne_config *config);
bool ne_config_save(ne_config *config, const char *data, ne_save_reason_flags flags);
bool ne_config_save_node(ne_config *config, ne_node_id node_id, const char *data, ne_save_reason_flags flags);
void ne_config_end_save(ne_config *config);

// JSON serialize/parse.
char *ne_node_settings_serialize(const ne_node_settings *settings);
bool ne_node_settings_parse_str(const char *str, ne_node_settings *settings);
char *ne_settings_serialize(const ne_settings *settings);
bool ne_settings_parse_str(const char *str, ne_settings *settings);

// EditorContext internal helpers.
void ne_editor_context_load_settings(ne_editor_context *editor);
void ne_editor_context_save_settings(ne_editor_context *editor);
ne_control ne_editor_context_build_control(ne_editor_context *editor, bool allow_offscreen);
void ne_editor_context_update_animations(ne_editor_context *editor);
void ne_editor_context_show_metrics(ne_editor_context *editor, const ne_control *control);

#endif /* NE_INTERNAL_H */
