#ifndef CHIBI_RENDER_SCENE3D_POINT_SHADOW_H
#define CHIBI_RENDER_SCENE3D_POINT_SHADOW_H

#include "platform/cute.h"

#include <stdbool.h>
#include <stddef.h>

enum
{
    /* Point lights that cast shadows in one view; matches scene3d.frag. */
    RENDER_SCENE3D_POINT_SHADOW_LIMIT = 4,
    /* One per axis direction: +x, -x, +y, -y, +z, -z. */
    RENDER_SCENE3D_POINT_SHADOW_FACES = 6
};

/* Point lights' shadow maps: six perspective depth faces around each light,
 * one atlas row per light, that the lit pass samples with hardware
 * comparison. */
typedef struct render_scene3d_point_shadow render_scene3d_point_shadow;
render_scene3d_point_shadow *render_scene3d_point_shadow_new();
void render_scene3d_point_shadow_free(render_scene3d_point_shadow *shadow);
/* Takes the lights that cast this frame, each its position and radius. */
void render_scene3d_point_shadow_set(render_scene3d_point_shadow *shadow, const CF_V4 *lights, int count);
/* Whether a box, a unit cube placed around a solid in the world, can cast
 * into any face of a light. A box around the light itself casts nothing, so
 * a lamp's own bulb leaves it lit. */
bool render_scene3d_point_shadow_near(const render_scene3d_point_shadow *shadow, int light, CF_M4x4 box);
/* Whether a box near a light can cast into one of its faces. */
bool render_scene3d_point_shadow_reaches(
    const render_scene3d_point_shadow *shadow,
    int light,
    int face,
    CF_M4x4 box
);
/* 3D draws issued between begin and end write one face's depth from its
 * light; the last light's last face renders only those draws. */
bool render_scene3d_point_shadow_begin(
    render_scene3d_point_shadow *shadow,
    int light,
    int face,
    char *error,
    size_t size
);
void render_scene3d_point_shadow_end(render_scene3d_point_shadow *shadow, int light, int face);
/* Binds the lights set for this frame for the lit pass; none leaves every
 * surface in their light. */
void render_scene3d_point_shadow_bind(render_scene3d_point_shadow *shadow);
void render_scene3d_point_shadow_unbind();

#endif
