#ifndef CHIBI_RENDER_MATERIAL_HEIGHT_H
#define CHIBI_RENDER_MATERIAL_HEIGHT_H

#include "render/shader_catalog.h"
#include "runtime/geometry/material_surface_mesh.h"

typedef struct render_material_height render_material_height;
/* Returns old when its immutable snapshot still matches. Otherwise next owns
 * a new snapshot; callers commit it only after geometry generation succeeds.
 * Pixel views borrow the resident image generation. Sample only immediately
 * after successful prepare, with catalogs quiescent until the build ends.
 * A failed prepare never permits sampling the previous generation. */
bool render_material_height_prepare(
    ecs_world *,
    ecs_entity,
    const Shader *,
    render_shader_parameter_cache **,
    render_material_height *old,
    render_material_height **next,
    char *error,
    size_t size
);
void render_material_height_free(render_material_height *);
/* Height's reference plane is PartPosition=(source X, 0, source Z), not the
 * subsequently displaced surface. Author shared channels in source X/Z. */
material_surface_height render_material_height_sampler(render_material_height *);

#endif
