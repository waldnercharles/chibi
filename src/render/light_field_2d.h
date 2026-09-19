#ifndef CHIBI_RENDER_LIGHT_FIELD_2D_H
#define CHIBI_RENDER_LIGHT_FIELD_2D_H

#include "platform/cute.h"
#include "render/view_2d.h"

#include <stdbool.h>

typedef struct
{
    CF_V2 position;
    float intensity;
    float radius;
    float color[4];
} render_point_light_2d;

typedef struct
{
    int source_count;
    int accepted_source_count;
    int clipped_source_count;
    int max_tile_occupancy;
    int tile_count;
    int capacity;
} render_light_field_stats;

typedef struct render_light_field_2d render_light_field_2d;

render_light_field_2d *render_light_field_2d_new();
void render_light_field_2d_free(render_light_field_2d *field);
const char *render_light_field_2d_error(const render_light_field_2d *field);
bool render_light_field_2d_resize(render_light_field_2d *field, int width, int height);
void render_light_field_2d_reset(render_light_field_2d *field);
bool render_light_field_2d_draw(
    render_light_field_2d *field,
    const render_point_light_2d *sources,
    int count,
    render_view_2d view
);
bool render_light_field_2d_composite(render_light_field_2d *field, CF_Texture base_emission);
CF_Texture render_light_field_2d_texture(const render_light_field_2d *field);
render_light_field_stats render_light_field_2d_stats(const render_light_field_2d *field);

#endif
