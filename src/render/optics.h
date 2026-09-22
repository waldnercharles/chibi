#ifndef CHIBI_RENDER_OPTICS_H
#define CHIBI_RENDER_OPTICS_H
#include "render/material_raster.h"

typedef struct render_optics render_optics;
typedef struct
{
    /* Y-down world position and unit direction; linear RGB, total power. */
    float position[2], direction[2], color[3], power, width, spread;
} render_optical_light;
typedef struct
{
    int max_size, rays, events;
} render_optical_budget;
typedef struct
{
    /* Angular mean radiance, isotropic source per world length, surface radiance. */
    CF_Texture direct, source, surface;
} render_optical_field;
render_optics *render_optics_new();
void render_optics_free(render_optics *optics);
const char *render_optics_error(const render_optics *optics);
bool render_optics_draw(
    render_optics *optics,
    const render_material_entity *entities,
    int count,
    const render_optical_light *lights,
    int light_count,
    render_optical_budget budget,
    CF_Texture medium,
    CF_Color scattering,
    render_view_2d view,
    int width,
    int height
);
render_optical_field render_optics_field(const render_optics *optics);
#endif
