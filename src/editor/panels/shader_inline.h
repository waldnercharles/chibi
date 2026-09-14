#ifndef CHIBI_EDITOR_SHADER_INLINE_H
#define CHIBI_EDITOR_SHADER_INLINE_H

#include "editor/workspace/editor.h"

#include <imgui/dcimgui.h>

/* Shader nodes stack outputs, options and full-width inputs. */
/* Shared controls retain distinct automation identities on the canvas. */
void ed_shader_track_item(const char *id);
const char *ed_shader_control_id(const char *id, char *buffer, size_t size);
bool ed_shader_graph(const editor *e);
float ed_shader_inline_height(const node *n);
float ed_shader_inline_width(const node *n);
void ed_draw_shader_inline(editor *e, node *n, ImVec2 min, ImVec2 max);

/* The Inspector uses the same strip, stop selection and color editor. */
void ed_draw_shader_ramp_controls(editor *e, node *n);

#endif
