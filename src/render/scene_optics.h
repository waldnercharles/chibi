#ifndef CHIBI_RENDER_SCENE_OPTICS_H
#define CHIBI_RENDER_SCENE_OPTICS_H
#include "ecs/ecs.h"
#include "render/material_raster.h"
#include "render/radiance.h"

typedef struct render_scene_optics render_scene_optics;
render_scene_optics *render_scene_optics_new();
void render_scene_optics_free(render_scene_optics *optics);
/* NULL on success; the adapter owns the returned diagnostic. */
const char *render_scene_optics_draw(
    render_scene_optics *optics,
    ecs_world *world,
    const render_material_entity *entities,
    int count,
    render_view_2d view,
    int width,
    int height,
    render_radiance_inputs *inputs
);
#endif
