#ifndef CHIBI_RENDER_SCENE3D_CASCADES_H
#define CHIBI_RENDER_SCENE3D_CASCADES_H

#include "platform/cute.h"
#include "render/scene3d.h"
#include "runtime/materials.h"

#include <stddef.h>

/* Screen light: the light the scene's own surfaces and emitters send to each
 * visible surface, gathered by radiance cascades over probes on the visible
 * surfaces. Probes sit every few pixels on the camera's depth; each cascade
 * traces rays four times as long in four times as many directions from
 * probes twice as far apart, and merges with the one above it. Screen tracing
 * marches the depth and takes the last frame's color where a ray meets it.
 * World tracing marches terrain volumes and a grid of the frame's emitting
 * particles, so light from off screen and rock the camera cannot see count.
 * The result is packed with the ambient occlusion, light in rgb and occlusion
 * in alpha. */
typedef struct render_scene3d_cascades render_scene3d_cascades;
render_scene3d_cascades *render_scene3d_cascades_new();
void render_scene3d_cascades_free(render_scene3d_cascades *cascades);
/* Takes the Environment's screen light for frames of the given pixel size.
 * Returns false with an error for an unknown mode, or for a mode that is on
 * without the ambient occlusion whose depth the probes stand on. */
bool render_scene3d_cascades_prepare(
    render_scene3d_cascades *cascades,
    const Environment *environment,
    int width,
    int height,
    char *error,
    size_t size
);
/* Starts the accumulated light over, as after a scene change. */
void render_scene3d_cascades_reset(render_scene3d_cascades *cascades);
bool render_scene3d_cascades_active(const render_scene3d_cascades *cascades);
/* Whether it traces the world, which needs the frame's opaque solids. */
bool render_scene3d_cascades_traces_world(const render_scene3d_cascades *cascades);
/* World tracing's inputs for this frame: the terrain volume the rays meet,
 * placed as scene3d draws it, and emitting particles. */
void render_scene3d_cascades_clear_world(render_scene3d_cascades *cascades);
void render_scene3d_cascades_set_rock(
    render_scene3d_cascades *cascades,
    materials_depth_volume view,
    float elevation,
    CF_V2 origin
);
/* Opaque solids that block world tracing's rays this frame, each the frame
 * of a unit cube; borrowed until draw. */
void render_scene3d_cascades_set_solids(
    render_scene3d_cascades *cascades,
    const CF_M4x4 *boxes,
    int count
);
/* A particle that lights world tracing's rays; ignored otherwise. */
void render_scene3d_cascades_add_emitter(
    render_scene3d_cascades *cascades,
    CF_V3 position,
    float radius,
    const float emission[3]
);
/* Traces, merges and gathers the cascades over the occlusion's depth and AO,
 * reading history, the last frame's linear color, for screen tracing.
 * Returns false with an error when its shaders cannot load. */
bool render_scene3d_cascades_draw(
    render_scene3d_cascades *cascades,
    const render_scene3d_camera *camera,
    CF_Texture depth,
    CF_Texture occlusion,
    CF_Texture history,
    char *error,
    size_t size
);
/* The light and occlusion the lit pass reads, the size of the frame. */
CF_Texture render_scene3d_cascades_result(const render_scene3d_cascades *cascades);

#endif
