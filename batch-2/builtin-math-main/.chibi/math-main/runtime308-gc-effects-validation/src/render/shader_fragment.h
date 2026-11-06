#ifndef CHIBI_SHADER_FRAGMENT_H
#define CHIBI_SHADER_FRAGMENT_H
#include "platform/cute.h"
#include "render/material_raster.h"
#include "render/shader_program.h"
char *render_shader_fragment_source();
int render_shader_fragment_prepare(
    render_programs *programs,
    const render_material_entity *entity,
    shader_error *error
);
void render_shader_fragment_draw(
    render_programs *programs,
    int program,
    const render_material_entity *entity,
    CF_V2 lighting
);
#endif
