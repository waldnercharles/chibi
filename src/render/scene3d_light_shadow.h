#ifndef CHIBI_RENDER_SCENE3D_LIGHT_SHADOW_H
#define CHIBI_RENDER_SCENE3D_LIGHT_SHADOW_H

#include "platform/cute.h"

#include <stdbool.h>
#include <stddef.h>

enum
{
    /* Point and spot lights that cast shadows in one view; matches
     * scene3d.frag. */
    RENDER_SCENE3D_LIGHT_SHADOW_LIMIT = 4,
    /* A point light's faces, one per axis direction: +x, -x, +y, -y, +z, -z. */
    RENDER_SCENE3D_LIGHT_SHADOW_FACES = 6
};

/* Point and spot lights' shadow maps: perspective depth faces from each light,
 * one atlas row per light, that the lit pass samples with hardware
 * comparison. A point light fills its row with six faces around it; a spot
 * light takes one face along its cone. */
typedef struct render_scene3d_light_shadow render_scene3d_light_shadow;
render_scene3d_light_shadow *render_scene3d_light_shadow_new();
void render_scene3d_light_shadow_free(render_scene3d_light_shadow *shadow);
/* Takes the lights that cast this frame: each one's position and radius, and
 * its cone as the lit pass reads it, with no axis for a point light. */
void render_scene3d_light_shadow_set(
    render_scene3d_light_shadow *shadow,
    const CF_V4 *lights,
    const CF_V4 *cones,
    int count
);
/* The faces a light renders. */
int render_scene3d_light_shadow_faces(const render_scene3d_light_shadow *shadow, int light);
/* Whether a box, a unit cube placed around a solid in the world, can cast
 * into any face of a light. A box around the light itself casts nothing, so
 * a lamp's own bulb leaves it lit. */
bool render_scene3d_light_shadow_near(const render_scene3d_light_shadow *shadow, int light, CF_M4x4 box);
/* A mesh's box is only a range bound, not solid occupancy. Keep a hollow
 * mesh containing the light: its actual triangles decide the shadow. */
bool render_scene3d_light_shadow_mesh_near(
    const render_scene3d_light_shadow *shadow,
    int light,
    CF_M4x4 box
);
/* Whether a box near a light can cast into one of its faces. */
bool render_scene3d_light_shadow_reaches(
    const render_scene3d_light_shadow *shadow,
    int light,
    int face,
    CF_M4x4 box
);
/* 3D draws issued between begin and end write one face's depth from its
 * light; the last light's last face renders only those draws. */
bool render_scene3d_light_shadow_begin(
    render_scene3d_light_shadow *shadow,
    int light,
    int face,
    char *error,
    size_t size
);
void render_scene3d_light_shadow_end(render_scene3d_light_shadow *shadow, int light, int face);
/* Binds the lights set for this frame for the lit pass; none leaves every
 * surface in their light. */
void render_scene3d_light_shadow_bind(render_scene3d_light_shadow *shadow);
void render_scene3d_light_shadow_unbind();

typedef struct
{
    CF_Texture map;
    /* Count, guarded face spread, near plane/radius, face texels. */
    CF_V4 settings;
} render_scene3d_light_shadow_snapshot;
render_scene3d_light_shadow_snapshot render_scene3d_light_shadow_read(render_scene3d_light_shadow *);

#endif
