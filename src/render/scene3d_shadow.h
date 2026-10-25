#ifndef CHIBI_RENDER_SCENE3D_SHADOW_H
#define CHIBI_RENDER_SCENE3D_SHADOW_H

#include "platform/cute.h"
#include "render/scene3d.h"

#include <stddef.h>

/* A depth-only texture and its canvas, which shadow passes draw into on one
 * layer and the lit pass compares against. Zeroed, it holds nothing. */
typedef struct
{
    CF_Texture map;
    CF_Canvas canvas;
    int width, height;
} render_scene3d_depth_map;
/* Grows the map to at least the given size, cleared to the far plane. */
void render_scene3d_depth_map_prepare(render_scene3d_depth_map *depth, int width, int height);
void render_scene3d_depth_map_free(render_scene3d_depth_map *depth);
/* Loads the depth-only shader shadow passes draw with, once. */
bool render_scene3d_depth_shader(CF_Shader *shader, char *error, size_t size);

/* One directional light's cascaded shadow map: depth-only squares of one
 * atlas, each covering a farther range of the view, that the lit pass samples
 * with hardware comparison. */
typedef struct render_scene3d_shadow render_scene3d_shadow;
render_scene3d_shadow *render_scene3d_shadow_new();
void render_scene3d_shadow_free(render_scene3d_shadow *shadow);
/* Fits orthographic light views to boxes, unit cubes placed around solids in
 * the world. The first `receivers` boxes are in view and set the areas the
 * cascades cover; the rest only cast into them. Spread, the tangent of half
 * the light's angular diameter, softens shadows with distance from their
 * caster; zero keeps them hard. Returns the cascade count, zero when the
 * camera sees none of the receivers. */
int render_scene3d_shadow_fit(
    render_scene3d_shadow *shadow,
    CF_V3 toward_light,
    float spread,
    const CF_M4x4 *boxes,
    int count,
    int receivers,
    const render_scene3d_camera *camera
);
/* Whether the fitted box at `box` can cast into a cascade. */
bool render_scene3d_shadow_reaches(const render_scene3d_shadow *shadow, int cascade, int box);
/* 3D draws issued between begin and end write one cascade's depth from the
 * light; the last cascade's end renders only those draws. */
bool render_scene3d_shadow_begin(render_scene3d_shadow *shadow, int cascade, char *error, size_t size);
void render_scene3d_shadow_end(render_scene3d_shadow *shadow, int cascade);
/* Binds the first `count` fitted cascades for the lit pass; none leaves every
 * surface in the light. */
void render_scene3d_shadow_bind(render_scene3d_shadow *shadow, int count);
void render_scene3d_shadow_unbind();

/* Immutable handles and transforms for another pass sampling these same
 * mesh shadows. The owner retains the textures through the frame. */
typedef struct
{
    CF_Texture map;
    CF_M4x4 matrices[4];
    CF_V4 settings; /* Cascade count, inverse tile side, atlas tile UV size. */
} render_scene3d_shadow_snapshot;
render_scene3d_shadow_snapshot render_scene3d_shadow_read(render_scene3d_shadow *, int count);

#endif
