#ifndef CBLUEPRINT_SHADER_FRAGMENT_H
#define CBLUEPRINT_SHADER_FRAGMENT_H
#include "platform/cute.h"
#include "render/material_raster.h"
CF_Shader render_shader_fragment_new();
void render_shader_fragment_draw(CF_Shader shader, const render_material_entity *entity);
#endif
