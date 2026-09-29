#ifndef CHIBI_RENDER_POST_2D_SCENE_H
#define CHIBI_RENDER_POST_2D_SCENE_H

#include "render/post_2d.h"
#include "runtime/engine.h"

/* The first GamePost2D's heat_haze, zero without one. */
float render_post_2d_heat_haze(ecs_world *world);
/* Fills the frame's grading and distortion from the scene's first GamePost2D,
 * its GameHeatHaze entities in view and the runtime's visual effects. The
 * caller sets exposure, bloom, dither and the distortion's textures. camera
 * is the visible rectangle. */
void render_post_2d_collect(
    ecs_world *world,
    const engine *runtime,
    render_view_2d camera,
    render_post_2d_settings *settings,
    render_post_2d_distortion *distortion
);

#endif
