#ifndef CHIBI_SHADER_DETAILS_H
#define CHIBI_SHADER_DETAILS_H
#include "editor/workspace/editor.h"
/* n may be NULL to draw shader asset settings. */
void ed_draw_shader_details(editor *e, node *n);
void ed_draw_shader_controls(editor *e, node *n);
void ed_draw_shader_image(editor *e, node *n);
#endif
