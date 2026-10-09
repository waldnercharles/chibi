#include "authoring/engine_preview.h"

#include <assert.h>
static void boundary(void *context, double now, double delta)
{
    game_preview_runtime *r = context;
    r->now = now;
    r->delta = delta;
    const engine_modules *m = game_world_modules(r->owner);
    if (m->boundary) {
        m->boundary(m->context, now, delta);
    }
}
static bool paused(void *context)
{
    game_preview_runtime *r = context;
    return game_source_paused(r->sources);
}
engine *authoring_engine_new(ecs_world *world, game_preview_runtime *r)
{
    if (!r) {
        return engine_new(world, NULL);
    }
    assert(r->ecs == world);
    engine_modules m = *game_world_modules(r->owner);
    m.context = r;
    m.boundary = boundary;
    m.paused = paused;
    assert(!r->engine);
    r->engine = engine_new(world, &m);
    return r->engine;
}
