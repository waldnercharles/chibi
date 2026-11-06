#ifndef CHIBI_RENDER_SPOT_LIGHTS_H
#define CHIBI_RENDER_SPOT_LIGHTS_H
#include "render/view_2d.h"
#include "runtime/engine.h"

#define RENDER_SPOT_LIGHT_LIMIT 4
/* Camera-aligned HRC coordinates: X right, Y up, Z elevation. These are
 * direct lights only: they never become isotropic emission or temporal bounce. */
typedef struct
{
    CF_V4 position_radius, color_intensity, direction_outer;
    float inner_cos, shadows, score;
} render_spot_light;
typedef struct
{
    render_spot_light lights[RENDER_SPOT_LIGHT_LIMIT];
    int count;
} render_spot_lights;

/* Uses propagated positions and the existing WORLD direction contract.
 * Keeps four lights with greatest estimated influence; rejects invalid cones. */
bool render_spot_lights_collect(
    ecs_world *,
    const engine *,
    render_view_2d,
    render_spot_lights *,
    char *,
    size_t
);
#endif
