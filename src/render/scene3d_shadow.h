#ifndef CHIBI_RENDER_SCENE3D_SHADOW_H
#define CHIBI_RENDER_SCENE3D_SHADOW_H

#include "render/scene3d.h"

#include <stddef.h>

/* One directional light's shadow map: a depth-only canvas that the lit pass
 * samples with hardware comparison. */
typedef struct render_scene3d_shadow render_scene3d_shadow;
render_scene3d_shadow *render_scene3d_shadow_new();
void render_scene3d_shadow_free(render_scene3d_shadow *shadow);
/* Fits an orthographic light view to boxes, the unit cubes that built-in
 * meshes scale into the world. The first `receivers` boxes are in view and set
 * the area the map covers; the rest only cast into it. False when the camera
 * sees none of the receivers. */
bool render_scene3d_shadow_fit(
    render_scene3d_shadow *shadow,
    CF_V3 toward_light,
    const CF_M4x4 *boxes,
    int count,
    int receivers,
    const render_scene3d_camera *camera
);
/* Whether a box can cast into the fitted map. */
bool render_scene3d_shadow_reaches(const render_scene3d_shadow *shadow, CF_M4x4 box);
/* 3D draws issued between begin and end write the fitted light's depth; end
 * renders only those. */
bool render_scene3d_shadow_begin(render_scene3d_shadow *shadow, char *error, size_t size);
void render_scene3d_shadow_end(render_scene3d_shadow *shadow);
/* Binds the map for the lit pass; disabled leaves every surface in the light. */
void render_scene3d_shadow_bind(render_scene3d_shadow *shadow, bool enabled);
void render_scene3d_shadow_unbind();

#endif
