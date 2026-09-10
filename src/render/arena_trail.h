#ifndef CHIBI_RENDER_ARENA_TRAIL_H
#define CHIBI_RENDER_ARENA_TRAIL_H

#include "ecs/ecs.h"
#include "platform/cute.h"
#include "render/arena3d.h"

struct engine;
typedef struct arena_trails arena_trails;
typedef struct
{
    CF_V3 from, to;
    CF_V4 color;
    float width, emission;
} arena_trail_segment;

arena_trails *render_arena_trails_new();
void render_arena_trails_free(arena_trails *trails);
void render_arena_trails_reset(arena_trails *trails);
void render_arena_trails_collect(
    arena_trails *trails,
    ecs_world *world,
    const struct engine *runtime,
    double time
);
const arena_trail_segment *render_arena_trails_segments(const arena_trails *trails, int *count);
/* Preflight/upload shared ribbons before scene submission; each source has one sortable range. */
bool render_arena_trails_prepare(
    arena_trails *trails,
    const render_arena_camera *camera,
    char *error,
    size_t size
);
void render_arena_trails_draw(const arena_trails *trails, const render_arena_camera *camera);

#endif
