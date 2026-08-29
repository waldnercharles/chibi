#include "engine.h"

#include "../core/array.h"
#include "../core/registry.h"
#include "../core/xalloc.h"
#include "../game/components.h" /* IWYU pragma: keep */

#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    engine_system_fn fn;
    void *user;
} engine_system;

struct engine
{
    ecs_world *world;
    game_live_world *live;
    engine_system *systems;
    engine_render_fn render;
    void *render_user;
    engine_focus focus;
    bool playing;
    bool step_requested;
    engine_frame_timing timing;
    double last_time;
    double delta_seconds;
    int has_time;
};

static double elapsed_ms(struct timespec start, struct timespec end)
{
    int64_t seconds = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    int64_t nanoseconds = (int64_t)end.tv_nsec - (int64_t)start.tv_nsec;
    return (double)(seconds * INT64_C(1000000000) + nanoseconds) / 1000000.0;
}

static int clock_now(struct timespec *out)
{
    return clock_gettime(CLOCK_MONOTONIC, out) == 0 ? 0 : -1;
}

engine *engine_new(ecs_world *world, game_live_world *live)
{
    if (!world) {
        return NULL;
    }
    engine *result = (engine *)xcalloc(1, sizeof *result);
    result->world = world;
    result->live = live;
    result->playing = true;
    return result;
}

void engine_free(engine *engine)
{
    if (!engine) {
        return;
    }
    afree(engine->systems);
    free(engine);
}

int engine_add_system(engine *engine, engine_system_fn fn, void *user)
{
    if (!engine || !fn) {
        return -1;
    }
    apush(engine->systems, (engine_system){ fn, user });
    return 0;
}

void engine_set_render_callback(engine *engine, engine_render_fn fn, void *user)
{
    if (!engine) {
        return;
    }
    engine->render = fn;
    engine->render_user = user;
}

int engine_set_focus(engine *engine, engine_focus focus)
{
    if (!engine || focus < ENGINE_FOCUS_NONE || focus > ENGINE_FOCUS_CANVAS) {
        return -1;
    }
    engine->focus = focus;
    return 0;
}

engine_focus engine_get_focus(const engine *engine)
{
    return engine ? engine->focus : ENGINE_FOCUS_NONE;
}

int engine_route_escape(engine *engine)
{
    if (!engine) {
        return -1;
    }
    engine->focus = ENGINE_FOCUS_NONE;
    return 0;
}

int engine_set_playing(engine *engine, bool playing)
{
    if (!engine) {
        return -1;
    }
    engine->playing = playing;
    if (playing) {
        engine->step_requested = false;
    }
    return 0;
}

bool engine_is_playing(const engine *engine)
{
    return engine && engine->playing;
}

int engine_step(engine *engine)
{
    if (!engine) {
        return -1;
    }
    engine->step_requested = true;
    return 0;
}

static void legacy_input_delta(const char *action, double value, double *x, double *y)
{
    *x = 0.0;
    *y = 0.0;
    if (strcmp(action, "MoveLeft") == 0 || strcmp(action, "MoveRight") == 0) {
        *x = value;
    }
    if (strcmp(action, "MoveUp") == 0 || strcmp(action, "MoveDown") == 0) {
        *y = value;
    }
}

int engine_dispatch_input(engine *engine, const char *action, double value, int pressed, flow_error *error)
{
    if (!engine || !action || engine->focus != ENGINE_FOCUS_VIEWPORT || !engine->playing) {
        return FLOW_ERR_STATE;
    }
    double x;
    double y;
    legacy_input_delta(action, pressed ? value : 0.0, &x, &y);
    CF_V2 delta = { (float)x, (float)y };
    pin_value inputs[] = {
        { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &delta },
    };
    const flow_inputs dispatch = registry_type_by_name("CF_V2")
                                   ? (flow_inputs){ inputs, 1, NULL }
                                   : (flow_inputs){ 0 };
    if (!engine->live) {
        return FLOW_ERR_STATE;
    }
    game_behavior_set_delta_seconds(engine->live->behaviors, engine->delta_seconds);
    return game_behavior_dispatch_all(engine->live->behaviors, "Input", dispatch, error);
}

int engine_frame(engine *engine, double monotonic_time)
{
    if (!engine) {
        return FLOW_ERR_ARGUMENT;
    }
    struct timespec start;
    struct timespec end;
    if (clock_now(&start) != 0) {
        return FLOW_ERR_STATE;
    }
    int result = 0;
    double delta_seconds = engine->has_time ? monotonic_time - engine->last_time : 0.0;
    if (delta_seconds < 0.0) {
        delta_seconds = 0.0;
    }
    engine->last_time = monotonic_time;
    engine->delta_seconds = delta_seconds;
    engine->has_time = 1;
    bool simulate = engine->playing || engine->step_requested;
    if (simulate && engine->live) {
        game_behavior_set_delta_seconds(engine->live->behaviors, delta_seconds);
        result = game_behavior_pump(engine->live->behaviors, monotonic_time);
    }
    engine->step_requested = false;
    if (clock_now(&end) != 0) {
        return FLOW_ERR_STATE;
    }
    engine->timing.behavior_ms = elapsed_ms(start, end);
    if (result < 0) {
        return result;
    }
    if (simulate) {
        if (clock_now(&start) != 0) {
            return FLOW_ERR_STATE;
        }
        result = ecs_world_apply_commands(engine->world);
        engine->timing.command_apply_count++;
        if (clock_now(&end) != 0) {
            return FLOW_ERR_STATE;
        }
        engine->timing.systems_ms = elapsed_ms(start, end);
        if (result != ECS_OK) {
            return result;
        }
        if (clock_now(&start) != 0) {
            return FLOW_ERR_STATE;
        }
        for (int i = 0; i < acount(engine->systems); i++) {
            engine->systems[i]
                .fn(engine->world, monotonic_time, engine->systems[i].user);
        }
    }
    if (engine->render) {
        engine->render(engine->render_user);
    }
    if (clock_now(&end) != 0) {
        return FLOW_ERR_STATE;
    }
    engine->timing.rendering_ms = elapsed_ms(start, end);
    engine->timing.frame++;
    return FLOW_OK;
}

const engine_frame_timing *engine_last_timing(const engine *engine)
{
    return engine ? &engine->timing : NULL;
}
