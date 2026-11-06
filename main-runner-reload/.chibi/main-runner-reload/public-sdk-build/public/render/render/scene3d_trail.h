#ifndef CHIBI_RENDER_SCENE3D_TRAIL_H
#define CHIBI_RENDER_SCENE3D_TRAIL_H

#include "ecs/ecs.h"
#include "platform/cute.h"
#include "render/scene3d.h"

struct engine;
typedef struct scene3d_trails scene3d_trails;
typedef struct
{
    CF_V3 from, to;
    CF_V4 color;
    float width, emission;
} scene3d_trail_segment;

scene3d_trails *render_scene3d_trails_new();
void render_scene3d_trails_free(scene3d_trails *trails);
void render_scene3d_trails_reset(scene3d_trails *trails);
void render_scene3d_trails_collect(
    scene3d_trails *trails,
    ecs_world *world,
    const struct engine *runtime,
    double time
);
const scene3d_trail_segment *render_scene3d_trails_segments(const scene3d_trails *trails, int *count);
/* Preflight/upload shared ribbons before scene submission; each source has one sortable range. */
bool render_scene3d_trails_prepare(
    scene3d_trails *trails,
    const render_scene3d_camera *camera,
    char *error,
    size_t size
);
void render_scene3d_trails_draw(const scene3d_trails *trails, const render_scene3d_camera *camera);

#endif
