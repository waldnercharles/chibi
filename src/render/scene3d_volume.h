#ifndef CHIBI_RENDER_SCENE3D_VOLUME_H
#define CHIBI_RENDER_SCENE3D_VOLUME_H

#include "render/scene3d.h"
#include "render/scene3d_light_shadow.h"
#include "render/scene3d_shadow.h"

/* Homogeneous single scattering through the actual camera-depth interval,
 * shadowed by the same 3D opaque/masked geometry as direct surface light. */
typedef struct render_scene3d_volume render_scene3d_volume;
typedef struct
{
    CF_V4 sun_direction, sun_color;
    const CF_V4 *positions, *colors, *cones;
    int count;
    render_scene3d_shadow_snapshot sun_shadow;
    render_scene3d_light_shadow_snapshot local_shadow;
} render_scene3d_volume_lights;

render_scene3d_volume *render_scene3d_volume_new();
void render_scene3d_volume_free(render_scene3d_volume *);
/* Pure validation, also usable before a graphics context is created. */
bool render_scene3d_volume_valid(const Environment *, char *error, size_t size);
bool render_scene3d_volume_prepare(
    render_scene3d_volume *,
    const Environment *,
    int width,
    int height,
    char *error,
    size_t size
);
bool render_scene3d_volume_active(const render_scene3d_volume *);
/* Caller submits opaque/masked solids between begin and end, using its
 * caster path so alpha-tested materials keep their actual silhouette. */
bool render_scene3d_volume_begin_depth(
    render_scene3d_volume *,
    const render_scene3d_camera *,
    char *error,
    size_t size
);
void render_scene3d_volume_end_depth(render_scene3d_volume *);
/* Computes the field immediately, then queues a premultiplied fullscreen
 * overlay after opaque surfaces and sky, before sorted translucents and
 * tone mapping. It does not flush the caller's surface queue. */
bool render_scene3d_volume_draw(
    render_scene3d_volume *,
    const render_scene3d_camera *,
    const render_scene3d_volume_lights *,
    char *error,
    size_t size
);

#endif
