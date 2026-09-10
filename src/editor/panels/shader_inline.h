#ifndef CHIBI_EDITOR_SHADER_INLINE_H
#define CHIBI_EDITOR_SHADER_INLINE_H

#include "editor/workspace/editor.h"

#include <imgui/dcimgui.h>

/* The ramp's common authoring controls fit below its ordinary graph pins. */
float ed_shader_inline_height(const node *n);
float ed_shader_inline_width(const node *n);
void ed_draw_shader_inline(editor *e, node *n, ImVec2 min, ImVec2 max);

/* The Inspector uses the same strip, stop selection and color editor. */
void ed_draw_shader_ramp_controls(editor *e, node *n);

#endif
