#ifndef CHIBI_RENDER_SCENE3D_OCCLUSION_H
#define CHIBI_RENDER_SCENE3D_OCCLUSION_H

#include "platform/cute.h"
#include "render/scene3d.h"

#include <stddef.h>

/* Screen-space ambient occlusion for the lit pass: the camera's depth, then
 * ground-truth ambient occlusion searched over it, then a blur that keeps to
 * each pixel's surface. */
typedef struct render_scene3d_occlusion render_scene3d_occlusion;
render_scene3d_occlusion *render_scene3d_occlusion_new();
void render_scene3d_occlusion_free(render_scene3d_occlusion *occlusion);
/* Takes the Environment's occlusion for frames of the given pixel size, which
 * an active occlusion needs. Returns false with an error for an unknown
 * quality or, while it is on, a radius that is not positive and finite. */
bool render_scene3d_occlusion_prepare(
    render_scene3d_occlusion *occlusion,
    const Environment *environment,
    int width,
    int height,
    char *error,
    size_t size
);
bool render_scene3d_occlusion_active(const render_scene3d_occlusion *occlusion);
/* 3D draws issued between begin and end write the camera's depth under the
 * caller's render state; end then computes the occlusion from it. */
bool render_scene3d_occlusion_begin(
    render_scene3d_occlusion *occlusion,
    const render_scene3d_camera *camera,
    char *error,
    size_t size
);
void render_scene3d_occlusion_end(render_scene3d_occlusion *occlusion);
/* Binds the occlusion for the lit pass's next draws, in alpha with no
 * screen light, or, for surfaces it doesn't describe and while it is off,
 * none, which leaves them open. */
void render_scene3d_occlusion_bind(render_scene3d_occlusion *occlusion, bool computed);
/* The camera's depth and the occlusion, in alpha, that end computed. */
CF_Texture render_scene3d_occlusion_depth(const render_scene3d_occlusion *occlusion);
CF_Texture render_scene3d_occlusion_map(const render_scene3d_occlusion *occlusion);
void render_scene3d_occlusion_unbind();

#endif
