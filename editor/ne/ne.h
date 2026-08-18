//------------------------------------------------------------------------------
// ne.h — C23 port of imgui_node_editor.h public API (imgui-node-editor,
// Michal Cichon). The `ax::NodeEditor::` namespace becomes the `ne_` prefix.
//
//   ed::NodeId / PinId / LinkId (uintptr_t)  -> ne_node_id / ne_pin_id / ne_link_id
//   ed::PinKind / FlowDirection / ...        -> ne_* enums
//   ed::Config / ed::Style                   -> ne_config / ne_style
//   ed::SetCurrentEditor / Begin / ...       -> ne_set_current_editor / ne_begin / ...
//
// Method overloads (PushStyleVar(float|ImVec2|ImVec4), QueryNewLink with and
// without color) become _f/_v2/_v4/_ex suffixed functions. Default arguments
// become explicit parameters.
//------------------------------------------------------------------------------
#ifndef NE_H
#define NE_H

#include <dcimgui.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define NE_VERSION "0.9.4"
#define NE_VERSION_NUM 000904

#ifndef NE_API
#define NE_API
#endif

    //------------------------------------------------------------------------------
    // IDs. Upstream NodeId/PinId/LinkId are opaque uintptr_t handles.
    typedef uintptr_t ne_node_id;
    typedef uintptr_t ne_pin_id;
    typedef uintptr_t ne_link_id;

    typedef enum ne_pin_kind
    {
        ne_pin_kind_input,
        ne_pin_kind_output
    } ne_pin_kind;

    typedef enum ne_flow_direction
    {
        ne_flow_direction_forward,
        ne_flow_direction_backward
    } ne_flow_direction;

    typedef enum ne_canvas_size_mode
    {
        ne_canvas_size_mode_fit_vertical_view, // Previous view will be scaled to fit new view on Y axis
        ne_canvas_size_mode_fit_horizontal_view, // Previous view will be scaled to fit new view on X axis
        ne_canvas_size_mode_center_only // Previous view will be centered on new view
    } ne_canvas_size_mode;

    // SaveReasonFlags (bitmask).
    typedef uint32_t ne_save_reason_flags;
    enum
    {
        ne_save_reason_none = 0x00000000,
        ne_save_reason_navigation = 0x00000001,
        ne_save_reason_position = 0x00000002,
        ne_save_reason_size = 0x00000004,
        ne_save_reason_selection = 0x00000008,
        ne_save_reason_add_node = 0x00000010,
        ne_save_reason_remove_node = 0x00000020,
        ne_save_reason_user = 0x00000040
    };

    typedef bool (*ne_config_save_settings)(
        const char *data,
        size_t size,
        ne_save_reason_flags reason,
        void *user_pointer
    );
    typedef size_t (*ne_config_load_settings)(char *data, void *user_pointer);

    typedef bool (*ne_config_save_node_settings)(
        ne_node_id node_id,
        const char *data,
        size_t size,
        ne_save_reason_flags reason,
        void *user_pointer
    );
    typedef size_t (*ne_config_load_node_settings)(ne_node_id node_id, char *data, void *user_pointer);

    typedef void (*ne_config_session)(void *user_pointer);

    typedef struct ne_config
    {
        const char *settings_file;
        ne_config_session begin_save_session;
        ne_config_session end_save_session;
        ne_config_save_settings save_settings;
        ne_config_load_settings load_settings;
        ne_config_save_node_settings save_node_settings;
        ne_config_load_node_settings load_node_settings;
        void *user_pointer;
        float *custom_zoom_levels; /* ckit dynamic array */
        int canvas_size_mode;      /* ne_canvas_size_mode */
        int drag_button_index; // Mouse button index drag action will react to (0-left, 1-right, 2-middle)
        int select_button_index; // Mouse button index select action will react to
        int navigate_button_index; // Mouse button index navigate action will react to
        int context_menu_button_index; // Mouse button index context menu action will react to
        bool enable_smooth_zoom;
        float smooth_zoom_power;
    } ne_config;

    typedef enum ne_style_color
    {
        ne_style_color_bg,
        ne_style_color_grid,
        ne_style_color_node_bg,
        ne_style_color_node_border,
        ne_style_color_hov_node_border,
        ne_style_color_sel_node_border,
        ne_style_color_node_sel_rect,
        ne_style_color_node_sel_rect_border,
        ne_style_color_hov_link_border,
        ne_style_color_sel_link_border,
        ne_style_color_highlight_link_border,
        ne_style_color_link_sel_rect,
        ne_style_color_link_sel_rect_border,
        ne_style_color_pin_rect,
        ne_style_color_pin_rect_border,
        ne_style_color_flow,
        ne_style_color_flow_marker,
        ne_style_color_group_bg,
        ne_style_color_group_border,

        ne_style_color_count
    } ne_style_color;

    typedef enum ne_style_var
    {
        ne_style_var_node_padding,
        ne_style_var_node_rounding,
        ne_style_var_node_border_width,
        ne_style_var_hovered_node_border_width,
        ne_style_var_selected_node_border_width,
        ne_style_var_pin_rounding,
        ne_style_var_pin_border_width,
        ne_style_var_link_strength,
        ne_style_var_source_direction,
        ne_style_var_target_direction,
        ne_style_var_scroll_duration,
        ne_style_var_flow_marker_distance,
        ne_style_var_flow_speed,
        ne_style_var_flow_duration,
        ne_style_var_pivot_alignment,
        ne_style_var_pivot_size,
        ne_style_var_pivot_scale,
        ne_style_var_pin_corners,
        ne_style_var_pin_radius,
        ne_style_var_pin_arrow_size,
        ne_style_var_pin_arrow_width,
        ne_style_var_group_rounding,
        ne_style_var_group_border_width,
        ne_style_var_highlight_connected_links,
        ne_style_var_snap_link_to_pin_dir,
        ne_style_var_hovered_node_border_offset,
        ne_style_var_selected_node_border_offset,

        ne_style_var_count
    } ne_style_var;

    typedef struct ne_style
    {
        ImVec4 node_padding;
        float node_rounding;
        float node_border_width;
        float hovered_node_border_width;
        float hover_node_border_offset;
        float selected_node_border_width;
        float selected_node_border_offset;
        float pin_rounding;
        float pin_border_width;
        float link_strength;
        ImVec2 source_direction;
        ImVec2 target_direction;
        float scroll_duration;
        float flow_marker_distance;
        float flow_speed;
        float flow_duration;
        ImVec2 pivot_alignment;
        ImVec2 pivot_size;
        ImVec2 pivot_scale;
        float pin_corners;
        float pin_radius;
        float pin_arrow_size;
        float pin_arrow_width;
        float group_rounding;
        float group_border_width;
        float highlight_connected_links;
        float snap_link_to_pin_dir;
        ImVec4 colors[ne_style_color_count];
    } ne_style;

    /* Fills *style with the upstream Style() defaults. */
    NE_API void ne_style_default(ne_style *style);
    /* Fills *config with the upstream Config() defaults. */
    NE_API void ne_config_default(ne_config *config);

    //------------------------------------------------------------------------------
    typedef struct ne_editor_context ne_editor_context;

    NE_API void ne_set_current_editor(ne_editor_context *ctx);
    NE_API ne_editor_context *ne_get_current_editor(void);
    NE_API ne_editor_context *ne_create_editor(const ne_config *config);
    NE_API void ne_destroy_editor(ne_editor_context *ctx);
    NE_API const ne_config *ne_get_config(ne_editor_context *ctx);

    NE_API ne_style *ne_get_style(void);
    NE_API const char *ne_get_style_color_name(ne_style_color color_index);

    NE_API void ne_push_style_color(ne_style_color color_index, const ImVec4 *color);
    NE_API void ne_pop_style_color(int count);

    NE_API void ne_push_style_var_f(ne_style_var var_index, float value);
    NE_API void ne_push_style_var_v2(ne_style_var var_index, ImVec2 value);
    NE_API void ne_push_style_var_v4(ne_style_var var_index, ImVec4 value);
    NE_API void ne_pop_style_var(int count);

    NE_API void ne_begin(const char *id, ImVec2 size);
    NE_API void ne_end(void);

    NE_API void ne_begin_node(ne_node_id id);
    NE_API void ne_begin_pin(ne_pin_id id, ne_pin_kind kind);
    NE_API void ne_pin_rect(ImVec2 a, ImVec2 b);
    NE_API void ne_pin_pivot_rect(ImVec2 a, ImVec2 b);
    NE_API void ne_pin_pivot_size(ImVec2 size);
    NE_API void ne_pin_pivot_scale(ImVec2 scale);
    NE_API void ne_pin_pivot_alignment(ImVec2 alignment);
    NE_API void ne_end_pin(void);
    NE_API void ne_group(ImVec2 size);
    NE_API void ne_end_node(void);

    NE_API bool ne_begin_group_hint(ne_node_id node_id);
    NE_API ImVec2 ne_get_group_min(void);
    NE_API ImVec2 ne_get_group_max(void);
    NE_API ImDrawList *ne_get_hint_foreground_draw_list(void);
    NE_API ImDrawList *ne_get_hint_background_draw_list(void);
    NE_API void ne_end_group_hint(void);

    NE_API ImDrawList *ne_get_node_background_draw_list(ne_node_id node_id);

    NE_API bool ne_link(ne_link_id id, ne_pin_id start_pin_id, ne_pin_id end_pin_id, ImVec4 color, float thickness);
    NE_API void ne_flow(ne_link_id link_id, ne_flow_direction direction);

    NE_API bool ne_begin_create(ImVec4 color, float thickness);
    NE_API bool ne_query_new_link(ne_pin_id *start_id, ne_pin_id *end_id);
    NE_API bool ne_query_new_link_ex(ne_pin_id *start_id, ne_pin_id *end_id, ImVec4 color, float thickness);
    NE_API bool ne_query_new_node(ne_pin_id *pin_id);
    NE_API bool ne_query_new_node_ex(ne_pin_id *pin_id, ImVec4 color, float thickness);
    NE_API bool ne_accept_new_item(void);
    NE_API bool ne_accept_new_item_ex(ImVec4 color, float thickness);
    NE_API void ne_reject_new_item(void);
    NE_API void ne_reject_new_item_ex(ImVec4 color, float thickness);
    NE_API void ne_end_create(void);

    NE_API bool ne_begin_delete(void);
    NE_API bool ne_query_deleted_link(ne_link_id *link_id, ne_pin_id *start_id, ne_pin_id *end_id);
    NE_API bool ne_query_deleted_node(ne_node_id *node_id);
    NE_API bool ne_accept_deleted_item(bool delete_dependencies);
    NE_API void ne_reject_deleted_item(void);
    NE_API void ne_end_delete(void);

    NE_API void ne_set_node_position(ne_node_id node_id, ImVec2 editor_position);
    NE_API void ne_set_group_size(ne_node_id node_id, ImVec2 size);
    NE_API ImVec2 ne_get_node_position(ne_node_id node_id);
    NE_API ImVec2 ne_get_node_size(ne_node_id node_id);
    NE_API void ne_center_node_on_screen(ne_node_id node_id);
    NE_API void ne_set_node_z_position(ne_node_id node_id, float z);
    NE_API float ne_get_node_z_position(ne_node_id node_id);

    NE_API void ne_restore_node_state(ne_node_id node_id);

    NE_API void ne_suspend(void);
    NE_API void ne_resume(void);
    NE_API bool ne_is_suspended(void);

    NE_API bool ne_is_active(void);

    NE_API bool ne_has_selection_changed(void);
    NE_API int ne_get_selected_object_count(void);
    NE_API int ne_get_selected_nodes(ne_node_id *nodes, int size);
    NE_API int ne_get_selected_links(ne_link_id *links, int size);
    NE_API bool ne_is_node_selected(ne_node_id node_id);
    NE_API bool ne_is_link_selected(ne_link_id link_id);
    NE_API void ne_clear_selection(void);
    NE_API void ne_select_node(ne_node_id node_id, bool append);
    NE_API void ne_select_link(ne_link_id link_id, bool append);
    NE_API void ne_deselect_node(ne_node_id node_id);
    NE_API void ne_deselect_link(ne_link_id link_id);

    NE_API bool ne_delete_node(ne_node_id node_id);
    NE_API bool ne_delete_link(ne_link_id link_id);

    NE_API bool ne_has_any_links_node(ne_node_id node_id);
    NE_API bool ne_has_any_links_pin(ne_pin_id pin_id);
    NE_API int ne_break_links_node(ne_node_id node_id);
    NE_API int ne_break_links_pin(ne_pin_id pin_id);

    NE_API void ne_navigate_to_content(float duration);
    NE_API void ne_navigate_to_selection(bool zoom_in, float duration);

    NE_API bool ne_show_node_context_menu(ne_node_id *node_id);
    NE_API bool ne_show_pin_context_menu(ne_pin_id *pin_id);
    NE_API bool ne_show_link_context_menu(ne_link_id *link_id);
    NE_API bool ne_show_background_context_menu(void);

    NE_API void ne_enable_shortcuts(bool enable);
    NE_API bool ne_are_shortcuts_enabled(void);

    NE_API bool ne_begin_shortcut(void);
    NE_API bool ne_accept_cut(void);
    NE_API bool ne_accept_copy(void);
    NE_API bool ne_accept_paste(void);
    NE_API bool ne_accept_duplicate(void);
    NE_API bool ne_accept_create_node(void);
    NE_API int ne_get_action_context_size(void);
    NE_API int ne_get_action_context_nodes(ne_node_id *nodes, int size);
    NE_API int ne_get_action_context_links(ne_link_id *links, int size);
    NE_API void ne_end_shortcut(void);

    NE_API float ne_get_current_zoom(void);

    NE_API ne_node_id ne_get_hovered_node(void);
    NE_API ne_pin_id ne_get_hovered_pin(void);
    NE_API ne_link_id ne_get_hovered_link(void);
    NE_API ne_node_id ne_get_double_clicked_node(void);
    NE_API ne_pin_id ne_get_double_clicked_pin(void);
    NE_API ne_link_id ne_get_double_clicked_link(void);
    NE_API bool ne_is_background_clicked(void);
    NE_API bool ne_is_background_double_clicked(void);
    NE_API int ne_get_background_click_button_index(void);
    NE_API int ne_get_background_double_click_button_index(void);

    NE_API bool ne_get_link_pins(ne_link_id link_id, ne_pin_id *start_pin_id, ne_pin_id *end_pin_id);

    NE_API bool ne_pin_had_any_links(ne_pin_id pin_id);

    NE_API ImVec2 ne_get_screen_size(void);
    NE_API ImVec2 ne_screen_to_canvas(ImVec2 pos);
    NE_API ImVec2 ne_canvas_to_screen(ImVec2 pos);

    NE_API int ne_get_node_count(void);
    NE_API int ne_get_ordered_node_ids(ne_node_id *nodes, int size);

#ifdef __cplusplus
}
#endif

#endif /* NE_H */
