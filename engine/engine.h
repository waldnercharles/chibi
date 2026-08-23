#ifndef CBLUEPRINT_ENGINE_H
#define CBLUEPRINT_ENGINE_H

#include "../ecs/ecs.h"
#include "../game/live.h"

#include <stdint.h>

typedef struct engine engine;
typedef struct engine_viewport engine_viewport;

typedef enum
{
    ENGINE_FOCUS_NONE,
    ENGINE_FOCUS_VIEWPORT,
    ENGINE_FOCUS_TEXT,
    ENGINE_FOCUS_CANVAS,
} engine_focus;

typedef struct
{
    uint64_t frame;
    double behavior_ms;
    double systems_ms;
    double rendering_ms;
    uint32_t command_apply_count;
} engine_frame_timing;

typedef void (*engine_system_fn)(ecs_world *world, double now, void *user);
typedef void (*engine_render_fn)(void *user);

engine *engine_new(ecs_world *world, game_live_world *live);
void engine_free(engine *engine);
int engine_add_system(engine *engine, engine_system_fn fn, void *user);
void engine_set_render_callback(engine *engine, engine_render_fn fn, void *user);
int engine_set_focus(engine *engine, engine_focus focus);
engine_focus engine_get_focus(const engine *engine);
int engine_route_escape(engine *engine);
int engine_set_player(engine *engine, ecs_entity entity);
int engine_dispatch_player_input(
    engine *engine,
    const char *action,
    double value,
    int pressed,
    flow_error *error
);
int engine_frame(engine *engine, double monotonic_time);
const engine_frame_timing *engine_last_timing(const engine *engine);

/* Host viewport surface. Resize requests are coalesced; the target is
 * recreated only when engine_viewport_commit_resize is called. */
engine_viewport *engine_viewport_new(int width, int height);
void engine_viewport_free(engine_viewport *viewport);
int engine_viewport_request_resize(engine_viewport *viewport, int width, int height);
int engine_viewport_commit_resize(engine_viewport *viewport);
int engine_viewport_draw(engine_viewport *viewport);
uint64_t engine_viewport_binding(const engine_viewport *viewport);
uint32_t engine_viewport_recreate_count(const engine_viewport *viewport);

#endif
