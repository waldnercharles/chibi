#ifndef CHIBI_EDITOR_UI_SHELL_H
#define CHIBI_EDITOR_UI_SHELL_H

#include "editor/ui/icons.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum
{
    EDITOR_UI_ASSET_FOLDER,
    EDITOR_UI_ASSET_PROTOTYPE,
    EDITOR_UI_ASSET_BEHAVIOR,
    EDITOR_UI_ASSET_SCENE,
    EDITOR_UI_ASSET_SYSTEM,
    EDITOR_UI_ASSET_DATA,
    EDITOR_UI_ASSET_FILE,
} editor_ui_asset_kind;

const char *editor_ui_asset_icon(editor_ui_asset_kind kind);
void editor_ui_identity(const char *name, const char *detail, editor_ui_asset_kind kind);
/* Lay out a wrapped label and size the following control, reserving action width. */
void editor_ui_property_label(const char *label, float action_width);
/* A vector row owns a table; pair a successful begin with end. */
bool editor_ui_begin_vector(const char *label);
void editor_ui_next_axis();
void editor_ui_end_vector();
bool editor_ui_vector_float(const char *id, float *value, int count, float min, float max);
bool editor_ui_axis_float(const char *label, float *value, int axis, bool overridden);
/* Section heading and bounded searchable checkbox popover. Pair true with end. */
bool editor_ui_begin_attachment_picker(const char *label, const char *id, char *query, size_t query_size);
void editor_ui_end_attachment_picker(int selected);

typedef enum
{
    EDITOR_UI_COMMAND_QUICK_OPEN,
    EDITOR_UI_COMMAND_SAVE,
    EDITOR_UI_COMMAND_SAVE_AS,
    EDITOR_UI_COMMAND_SAVE_ALL,
    EDITOR_UI_COMMAND_RELOAD,
    EDITOR_UI_COMMAND_RESTART_PREVIEW,
    EDITOR_UI_COMMAND_UNDO,
    EDITOR_UI_COMMAND_REDO,
    EDITOR_UI_COMMAND_NEW_PROTOTYPE,
    EDITOR_UI_COMMAND_NEW_BEHAVIOR,
    EDITOR_UI_COMMAND_FOCUS_SCENE,
    EDITOR_UI_COMMAND_FOCUS_MY_BEHAVIOR_ASSET,
    EDITOR_UI_COMMAND_FOCUS_INSPECTOR,
    EDITOR_UI_COMMAND_FOCUS_COMPILER_RESULTS,
    EDITOR_UI_COMMAND_FOCUS_OUTPUT_LOG,
    EDITOR_UI_COMMAND_REFRESH_CONTENT,
    EDITOR_UI_COMMAND_RUNTIME_PLAY_PAUSE,
    EDITOR_UI_COMMAND_RUNTIME_STOP,
    EDITOR_UI_COMMAND_RUNTIME_STEP,
    EDITOR_UI_COMMAND_RUNTIME_RESTART,
    EDITOR_UI_COMMAND_RUNTIME_TARGET_SCENE,
    EDITOR_UI_COMMAND_RUNTIME_TARGET_PREVIEW,
} editor_ui_command;

typedef int (*editor_ui_command_handler)(editor_ui_command command, void *user);

typedef enum editor_ui_pane
{
    EDITOR_UI_PANE_CONTENT,
    EDITOR_UI_PANE_WORLD_OUTLINER,
    EDITOR_UI_PANE_MY_BEHAVIOR_ASSET,
    EDITOR_UI_PANE_INSPECTOR,
    EDITOR_UI_PANE_NODE_CANVAS,
    EDITOR_UI_PANE_SCENE,
    EDITOR_UI_PANE_COMPILER_RESULTS,
    EDITOR_UI_PANE_OUTPUT_LOG,
    EDITOR_UI_PANE_PERFORMANCE,
    EDITOR_UI_PANE_COUNT,
} editor_ui_pane;

typedef struct
{
    float x;
    float y;
    float width;
    float height;
} editor_ui_rect;

typedef enum editor_ui_focus_target
{
    EDITOR_UI_FOCUS_NONE,
    EDITOR_UI_FOCUS_SCENE,
    EDITOR_UI_FOCUS_WORLD_OUTLINER,
    EDITOR_UI_FOCUS_NODE_CANVAS,
    EDITOR_UI_FOCUS_CONTENT_DRAWER,
    EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET,
    EDITOR_UI_FOCUS_INSPECTOR,
    EDITOR_UI_FOCUS_COMPILER_RESULTS,
    EDITOR_UI_FOCUS_OUTPUT_LOG,
    EDITOR_UI_FOCUS_PERFORMANCE,
} editor_ui_focus_target;

/* Pending until the window takes it immediately before Begin. NONE clears. */
void editor_ui_request_focus(editor_ui_focus_target target);
bool editor_ui_take_focus(editor_ui_focus_target target);

bool editor_ui_content_drawer_open(void);
void editor_ui_set_content_drawer_open(bool open);
bool editor_ui_pane_visible(editor_ui_pane pane);
void editor_ui_set_pane_visible(editor_ui_pane pane, bool visible);
/* Focus Scene when play begins, preserving both document panes and sidebars. */
void editor_ui_set_scene_play_expanded(bool enabled);
/* Requires editor_ui_pane_visible() to be true. Always pair this call with
 * ImGui_End(), including when ImGui returns false. A pane closed through its
 * title bar updates its View-menu state. */
bool editor_ui_begin_pane(editor_ui_pane pane, const char *name, int flags);
/* One host-owned Navigator with Files, Scene and optional Document destinations.
 * outline is the preferred Scene/Document pane on entry and the user choice on return.
 * On true, draw the selected body then call end.
 * On false, the window has already been closed internally. */
bool editor_ui_begin_navigator(editor_ui_pane *outline, bool has_document, editor_ui_pane *selected);
/* Workspace history buttons; request is -1 Back, +1 Forward, 0 none. */
void editor_ui_set_navigation_available(bool back, bool forward);
int editor_ui_take_navigation();
void editor_ui_request_navigation(int direction);
void editor_ui_set_document_bar_visible(bool visible);
bool editor_ui_document_bar_visible();
/* Pair with ImGui_End(), even when the contents are clipped. */
bool editor_ui_begin_document_bar();
/* Consumes the retained toolbar's Open Documents action. */
bool editor_ui_take_document_list_request();
bool editor_ui_navigator_visible(void);
editor_ui_pane editor_ui_navigator_tab(editor_ui_pane outline);
void editor_ui_restore_navigator_tab(editor_ui_pane tab);
void editor_ui_end_navigator();
bool editor_ui_pane_rect(editor_ui_pane pane, editor_ui_rect *out);
/* User clicks in document panes; reading consumes the pending interaction. */
editor_ui_pane editor_ui_take_document_interaction();
/* Pane controls can consume a click without changing the inspected document. */
void editor_ui_cancel_document_interaction(editor_ui_pane pane);
void editor_ui_show_graph_and_scene(void);
bool editor_ui_focus_mode();
void editor_ui_set_focus_mode(bool enabled);
bool editor_ui_reduced_motion();
void editor_ui_set_reduced_motion(bool enabled);
bool editor_ui_inline_edge_nodes();
void editor_ui_set_inline_edge_nodes(bool enabled);

/* Register workspace preferences before the first ImGui frame loads settings. */
void editor_ui_initialize(bool persist);
/* Show converted controls only, without changing workspace visibility preferences. */
void editor_ui_set_design_preview(bool enabled);
void editor_ui_prepare(void);
/* Compose retained UI after document panes and before presenting the frame. */
void editor_ui_render();
bool editor_ui_captures_keyboard();
void editor_ui_reset_layout(void);
void editor_ui_set_framebuffer_target(bool target);
void editor_ui_set_framebuffer_size(int width, int height);
/* Input coordinates are framebuffer pixels. They are normalized to ImGui's
 * logical coordinates before the next editor frame consumes them. */
void editor_ui_set_mouse_input(float framebuffer_x, float framebuffer_y, bool primary_down);
/* Pane geometry is reported in framebuffer pixels. */
float editor_ui_left_pane_width(void);
float editor_ui_left_pane_splitter_x(void);
float editor_ui_left_pane_center_y(void);
void editor_ui_set_command_handler(editor_ui_command_handler handler, void *user);
void editor_ui_set_graph_debug_header(void (*draw)(void *), void *user);
void editor_ui_draw_graph_debug_header();
int editor_ui_run_command(editor_ui_command command);
void editor_ui_set_runtime_state(bool playing, bool session);
void editor_ui_set_runtime_target(const char *label, bool preview_available, bool locked);
bool editor_ui_runtime_playing();
bool editor_ui_runtime_session();
void editor_ui_set_history_available(bool undo, bool redo);
void editor_ui_set_preview_status(const char *label, const char *detail, bool restart);
void editor_ui_set_runtime_status(const char *label);
/* Full-width disclosure; when true, pair with ImGui_TreePop(). */
bool editor_ui_disclosure(const char *label, const char *id);
/* Expanded by default. A non-NULL removed enables the hover/focus trash action.
 * Tracks id and id.remove; callers pair an open header with TreePop. */
bool editor_ui_component_header(const char *label, const char *id, bool *removed);
bool editor_ui_section_header(const char *label, const char *action_id);
/* Register the last rendered item by a stable semantic ID. Queries describe
 * the last frame, never trigger actions, and use ImGui input coordinates. */
void editor_ui_track_item(const char *id);
void editor_ui_track_rect(const char *id, editor_ui_rect rect);
bool editor_ui_item_center(const char *id, float *x, float *y);
bool editor_ui_item_rect(const char *id, editor_ui_rect *out);

/* A 200ms fade for hover and focus. First changed frame retains current alpha.
 * Call in the stable owner window; hidden controls must stop accepting input
 * immediately while their returned opacity fades to zero. */
float editor_ui_controls_opacity(const char *id, bool visible);
/* Opt-in pointer popup entry. Successful begin must pair with this end. */
void editor_ui_open_popup(const char *id);
bool editor_ui_begin_popup(const char *id);
void editor_ui_end_popup();

bool editor_ui_icon_button(const char *id, editor_icon glyph, bool enabled);
/* Square icon control for floating toolbars; size is in logical pixels. */
void editor_ui_value_label(const char *label, const char *number);
bool editor_ui_slider_float(const char *id, const char *label, float *value, float min, float max);
bool editor_ui_icon_button_sized(const char *id, editor_icon glyph, bool enabled, float size);
bool editor_ui_right_icon_button(const char *id, editor_icon glyph);
/* Tracks id and id.clear; clearing returns true and restores input focus. */
bool editor_ui_search_field(const char *id, char *buffer, size_t buffer_size, const char *hint);

/* Host-owned copies contain ImGui layout and Chibi workspace preferences.
 * Import and handler rebinding run only when the candidate becomes active. */
char *editor_ui_checkpoint();
bool editor_ui_restore_checkpoint(const char *ini);
void editor_ui_deactivate();

#endif
