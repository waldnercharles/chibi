#ifndef CBLUEPRINT_EDITOR_UI_SHELL_H
#define CBLUEPRINT_EDITOR_UI_SHELL_H

#include <stdbool.h>
#include <stddef.h>

typedef enum
{
    EDITOR_UI_COMMAND_SAVE,
    EDITOR_UI_COMMAND_SAVE_AS,
    EDITOR_UI_COMMAND_SAVE_ALL,
    EDITOR_UI_COMMAND_RELOAD,
    EDITOR_UI_COMMAND_UNDO,
    EDITOR_UI_COMMAND_REDO,
    EDITOR_UI_COMMAND_NEW_PROTOTYPE,
    EDITOR_UI_COMMAND_NEW_BEHAVIOR,
    EDITOR_UI_COMMAND_FOCUS_SCENE,
    EDITOR_UI_COMMAND_FOCUS_SCENE_RUNTIME,
    EDITOR_UI_COMMAND_FOCUS_COMPONENTS,
    EDITOR_UI_COMMAND_FOCUS_MY_BEHAVIOR_ASSET,
    EDITOR_UI_COMMAND_FOCUS_INSPECTOR,
    EDITOR_UI_COMMAND_FOCUS_COMPILER_RESULTS,
    EDITOR_UI_COMMAND_FOCUS_OUTPUT_LOG,
    EDITOR_UI_COMMAND_REFRESH_CONTENT,
} editor_ui_command;

typedef int (*editor_ui_command_handler)(editor_ui_command command, void *user);

typedef enum editor_ui_focus_target
{
    EDITOR_UI_FOCUS_NONE,
    EDITOR_UI_FOCUS_SCENE,
    EDITOR_UI_FOCUS_SCENE_RUNTIME,
    EDITOR_UI_FOCUS_NODE_CANVAS,
    EDITOR_UI_FOCUS_CONTENT_DRAWER,
    EDITOR_UI_FOCUS_COMPONENTS,
    EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET,
    EDITOR_UI_FOCUS_INSPECTOR,
    EDITOR_UI_FOCUS_COMPILER_RESULTS,
    EDITOR_UI_FOCUS_OUTPUT_LOG,
} editor_ui_focus_target;

/* Pending until the window takes it immediately before Begin. NONE clears. */
void editor_ui_request_focus(editor_ui_focus_target target);
bool editor_ui_take_focus(editor_ui_focus_target target);

bool editor_ui_content_drawer_open(void);
void editor_ui_set_content_drawer_open(bool open);

void editor_ui_prepare(void);
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
bool editor_ui_section_header(const char *label, const char *action_id);
/* Register the last rendered item by a stable semantic ID. Queries describe
 * the last frame, never trigger actions, and use ImGui input coordinates. */
void editor_ui_track_item(const char *id);
bool editor_ui_item_center(const char *id, float *x, float *y);

bool editor_ui_icon_button(const char *id, int glyph, bool enabled);
bool editor_ui_right_icon_button(const char *id, int glyph);
bool editor_ui_search_field(const char *id, char *buffer, size_t buffer_size, const char *hint);

#endif
