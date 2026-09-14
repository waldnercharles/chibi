#include "runtime/engine.h"

#include "graph/document/events.h"
#include "render/sdf_motion.h"
#include "runtime/components.h" /* IWYU pragma: keep */
#include "runtime/engine_input.h"
#include "runtime/generated.h"
#include "runtime/scenes/physics.h"
#include "runtime/visual_motion.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct
{
    engine_system_fn fn;
    void *user;
} engine_system;

typedef enum
{
    STEP_IDLE,
    STEP_BEHAVIORS,
    STEP_CONTACTS
} step_phase;

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
    engine_input *input;
    step_phase phase;
    engine_frame_timing timing;
    double last_time;
    double accumulator;
    bool has_time;
    bool debug_paused;
    char error[256];
};

const game_visibility *engine_visibility(const engine *engine)
{
    return engine && engine->live ? engine->live->visibility : NULL;
}

game_audio *engine_audio(const engine *engine)
{
    return game_behavior_audio(engine->live->behaviors);
}

bool engine_entity_visible(const engine *engine, ecs_world *world, ecs_entity entity)
{
    const GameRenderState *state = GAME_GameRenderState_get_const(world, entity);
    if (!engine || !state) {
        return true;
    }
    if (state->hidden) {
        return false;
    }
    const GameTransform *position = GAME_GameTransform_get_const(world, entity);
    const game_visibility *visibility = engine_visibility(engine);
    return !state->visibility_culled || !position || !visibility ||
           game_visibility_at(visibility, *position).visible;
}

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
    result->input = engine_input_new(world, live ? live->behaviors : NULL);
    if (live) {
        game_behavior_set_delta_seconds(live->behaviors, ENGINE_FIXED_SECONDS);
    }
    return result;
}

void engine_free(engine *engine)
{
    if (!engine) {
        return;
    }
    afree(engine->systems);
    engine_input_free(engine->input);
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

void engine_reset(engine *engine)
{
    engine->timing = (engine_frame_timing){ .alpha = 1 };
    engine->last_time = 0;
    engine->accumulator = 0;
    engine->has_time = false;
    engine->debug_paused = false;
    engine->step_requested = false;
    engine->phase = STEP_IDLE;
    engine->error[0] = 0;
    engine_input_reset(engine->input);
    if (engine->live) {
        game_save_discard(game_behavior_save(engine->live->behaviors));
        game_behavior_set_time(engine->live->behaviors, 0);
        game_behavior_set_delta_seconds(engine->live->behaviors, ENGINE_FIXED_SECONDS);
        game_physics_reset(engine->live->physics);
        game_visibility_reset(engine->live->visibility);
        game_audio_reset(engine_audio(engine));
    }
}

int engine_set_focus(engine *engine, engine_focus focus)
{
    if (!engine || focus < ENGINE_FOCUS_NONE || focus > ENGINE_FOCUS_CANVAS) {
        return -1;
    }
    if (engine->focus == ENGINE_FOCUS_VIEWPORT && focus != ENGINE_FOCUS_VIEWPORT) {
        engine_input_clear(engine->input);
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
    return engine_set_focus(engine, ENGINE_FOCUS_NONE);
}

int engine_sample_controls(engine *engine, uint64_t controls, bool *firing)
{
    return engine_input_controls(engine->input, controls, firing);
}

int engine_set_playing(engine *engine, bool playing)
{
    if (!engine) {
        return -1;
    }
    if (engine->playing == playing) {
        return 0;
    }
    engine->playing = playing;
    engine->timing.alpha = 1;
    engine->accumulator = 0;
    engine->has_time = false;
    engine->step_requested = false;
    if (!playing) {
        engine_input_clear(engine->input);
    }
    return 0;
}

bool engine_is_playing(const engine *engine)
{
    return engine && engine->playing;
}

int engine_step(engine *engine)
{
    if (!engine || engine->playing) {
        return FLOW_ERR_STATE;
    }
    engine->step_requested = true;
    return 0;
}

int engine_dispatch_input(engine *engine, const char *action, double value, int pressed, flow_error *error)
{
    if (error) {
        *error = (flow_error){ 0 };
    }
    if (!engine || !action || !isfinite(value)) {
        return FLOW_ERR_ARGUMENT;
    }
    if (engine->focus != ENGINE_FOCUS_VIEWPORT || !engine->playing || !engine->live) {
        return FLOW_ERR_STATE;
    }
    int result = engine_input_action(engine->input, action, pressed != 0);
    if (result < 0 && error) {
        *error = (flow_error){ .code = result, .message = "Unknown local input action." };
    }
    return result;
}

int engine_sample_movement(engine *engine, float x, float y)
{
    if (!engine || !isfinite(x) || !isfinite(y)) {
        return FLOW_ERR_ARGUMENT;
    }
    if (engine->focus != ENGINE_FOCUS_VIEWPORT || !engine->playing || !engine->live) {
        return FLOW_ERR_STATE;
    }
    engine_input_movement(engine->input, (CF_V2){ x, y });
    return FLOW_OK;
}

int engine_sample_aim(engine *engine, CF_V2 aim, bool firing, bool point)
{
    if (!engine || !isfinite(aim.x) || !isfinite(aim.y)) {
        return FLOW_ERR_ARGUMENT;
    }
    if (!engine->live) {
        return FLOW_ERR_STATE;
    }
    bool active = engine->playing && engine->focus == ENGINE_FOCUS_VIEWPORT;
    engine_input_aim(engine->input, active ? aim : (CF_V2){ 0 }, active && firing, active && point);
    return FLOW_OK;
}

static int flush_initializers(engine *engine)
{
    if (!engine->live) {
        return FLOW_OK;
    }
    int result = game_entities_flush_initializers(
        engine->live->entities,
        engine->timing.simulation_time
    );
    if (result == FLOW_ERR_STEP_BUDGET) {
        snprintf(engine->error, sizeof engine->error, "Spawn initialization exceeded its work budget.");
    }
    return result;
}

static int pump_events(engine *engine)
{
    game_entities *entities = engine->live->entities;
    int result = FLOW_OK;
    for (int batch = 0; batch < ENGINE_EVENT_BATCHES_MAX; batch++) {
        if (!game_entities_pending_events(entities)) {
            break;
        }
        result = game_entities_dispatch_events(entities);
        if (result < 0) {
            return result;
        }
        result = flush_initializers(engine);
        if (result != 0 || game_behavior_paused(engine->live->behaviors)) {
            return result < 0 ? result : FLOW_PAUSED_RESULT;
        }
    }
    engine->timing.event_backlog = game_entities_pending_events(entities);
    return FLOW_OK;
}

static int update_visual_motion(engine *engine, float delta)
{
    int result = game_visual_motion_tick(engine->world, delta);
    if (result < 0) {
        snprintf(
            engine->error,
            sizeof engine->error,
            "%s",
            result == FLOW_ERR_STATE
                ? "Visual Motion requires Visual Transform and 3D Geometry."
                : "Visual Motion requires finite values and nonnegative speed "
                  "and timing."
        );
    }
    return result;
}

static int finish_systems(engine *engine)
{
    int result = FLOW_OK;
    if (engine->live) {
        if (engine->phase == STEP_BEHAVIORS) {
            struct timespec start, end;
            if (clock_now(&start) != 0) {
                return FLOW_ERR_STATE;
            }
            result = game_physics_tick(engine->live->physics, (float)ENGINE_FIXED_SECONDS);
            if (clock_now(&end) != 0) {
                return FLOW_ERR_STATE;
            }
            engine->timing.physics_ms += elapsed_ms(start, end);
            if (result < 0) {
                snprintf(
                    engine->error,
                    sizeof engine->error,
                    "%s",
                    game_physics_error(engine->live->physics)
                );
                return result;
            }
            engine->phase = STEP_CONTACTS;
        }
        struct timespec start, end;
        if (clock_now(&start) != 0) {
            return FLOW_ERR_STATE;
        }
        result = pump_events(engine);
        if (clock_now(&end) != 0) {
            return FLOW_ERR_STATE;
        }
        engine->timing.events_ms += elapsed_ms(start, end);
        if (result != 0) {
            return result < 0 ? result : FLOW_OK;
        }
    }
    result = ecs_world_apply_commands(engine->world);
    engine->timing.command_apply_count++;
    if (result != ECS_OK) {
        return result;
    }
    for (int i = 0; i < acount(engine->systems); i++) {
        engine->systems[i]
            .fn(engine->world,
                engine->timing.simulation_time,
                engine->systems[i].user);
    }
    /* Begin and reactions can change motion after integration. Resolve those
       authored values before presentation without advancing their clocks. */
    if (engine->live && (result = update_visual_motion(engine, 0)) < 0) {
        return result;
    }
    if (engine->live) {
        const char *error = game_visibility_update(engine->live->visibility, ENGINE_FIXED_SECONDS);
        if (error) {
            snprintf(engine->error, sizeof engine->error, "%s", error);
            return FLOW_ERR_NODE;
        }
    }
    engine->phase = STEP_IDLE;
    return FLOW_OK;
}

static int finish_step(engine *engine)
{
    struct timespec start, end;
    if (clock_now(&start) != 0) {
        return FLOW_ERR_STATE;
    }
    int result = finish_systems(engine);
    if (clock_now(&end) != 0) {
        return FLOW_ERR_STATE;
    }
    engine->timing.systems_ms += elapsed_ms(start, end);
    return result;
}

static int continue_step(engine *engine)
{
    game_live_world *live = engine->live;
    if (!live) {
        return finish_step(engine);
    }
    game_behavior_set_time(live->behaviors, engine->timing.simulation_time);
    game_behavior_set_delta_seconds(live->behaviors, ENGINE_FIXED_SECONDS);
    int result = flush_initializers(engine);
    if (result != 0) {
        return result < 0 ? result : FLOW_OK;
    }
    if (engine->phase == STEP_IDLE) {
        struct timespec start, end;
        if (clock_now(&start) != 0) {
            return FLOW_ERR_STATE;
        }
        result = game_particles_tick(live->particles, ENGINE_FIXED_SECONDS);
        if (result >= 0) {
            result = game_sdf_motion_tick(engine->world, ENGINE_FIXED_SECONDS);
        }
        if (result >= 0) {
            result = update_visual_motion(engine, (float)ENGINE_FIXED_SECONDS);
        }
        if (clock_now(&end) != 0) {
            return FLOW_ERR_STATE;
        }
        engine->timing.systems_ms += elapsed_ms(start, end);
        if (result >= 0) {
            result = engine_input_apply(engine->input);
        }
        if (result >= 0) {
            result = game_entities_dispatch_events(live->entities);
        }
        if (result >= 0) {
            result = game_behavior_update(live->behaviors, NULL);
        }
        if (result >= 0) {
            result = game_behavior_pump(live->behaviors, engine->timing.simulation_time);
        }
        if (result >= 0) {
            result = game_entities_dispatch_events(live->entities);
        }
        engine->phase = STEP_BEHAVIORS;
    } else {
        /* Resume a held activation before completing its remaining step stages. */
        result = game_behavior_pump(live->behaviors, engine->timing.simulation_time);
    }
    if (result < 0) {
        return result;
    }
    result = flush_initializers(engine);
    if (result != 0 || game_behavior_paused(live->behaviors)) {
        return result < 0 ? result : FLOW_OK;
    }
    return finish_step(engine);
}

static int fixed_step(engine *engine)
{
    engine->timing.simulation_steps++;
    engine->timing.simulation_time = engine->timing.simulation_steps * ENGINE_FIXED_SECONDS;
    engine->timing.steps++;
    return continue_step(engine);
}

static void discard_backlog(engine *engine)
{
    double whole = floor((engine->accumulator + 1e-10) / ENGINE_FIXED_SECONDS) *
                   ENGINE_FIXED_SECONDS;
    engine->timing.dropped_seconds += whole;
    engine->accumulator = fmax(0, engine->accumulator - whole);
}

static int advance_frame(engine *engine, double monotonic_time)
{
    if (!engine || !isfinite(monotonic_time)) {
        return FLOW_ERR_ARGUMENT;
    }
    struct timespec start, end;
    if (clock_now(&start) != 0) {
        return FLOW_ERR_STATE;
    }
    double elapsed = engine->has_time ? fmax(0, monotonic_time - engine->last_time)
                                      : 0;
    if (!isfinite(elapsed)) {
        return FLOW_ERR_ARGUMENT;
    }
    engine->last_time = engine->has_time ? fmax(engine->last_time, monotonic_time)
                                         : monotonic_time;
    engine->has_time = true;
    engine->timing.steps = 0;
    engine->timing.behavior_ms = 0;
    engine->timing.rendering_ms = 0;
    engine->timing.systems_ms = 0;
    engine->timing.physics_ms = 0;
    engine->timing.events_ms = 0;
    if (engine->live) {
        game_behavior_profile_begin(engine->live->behaviors);
    }
    int result = engine->live ? game_entities_flush(engine->live->entities) : FLOW_OK;
    if (result < 0) {
        return result;
    }
    bool paused = engine->live && game_behavior_paused(engine->live->behaviors);
    if (!paused && engine->debug_paused) {
        engine->timing.dropped_seconds += elapsed + engine->accumulator;
        engine->accumulator = 0;
        elapsed = 0;
    }
    if (!paused &&
        (engine->playing || engine->step_requested || engine->phase != STEP_IDLE)) {
        result = flush_initializers(engine);
        if (result < 0) {
            return result;
        }
        paused = engine->live && game_behavior_paused(engine->live->behaviors);
        if (!result && !paused && engine->phase != STEP_IDLE) {
            result = continue_step(engine);
            if (result < 0) {
                return result;
            }
            paused = game_behavior_paused(engine->live->behaviors);
        }
    }
    if (!paused && (engine->playing || engine->step_requested)) {
        if (engine->playing) {
            engine->timing.dropped_seconds += fmax(0, elapsed - ENGINE_FRAME_SECONDS_MAX);
            engine->accumulator += fmin(elapsed, ENGINE_FRAME_SECONDS_MAX);
        }
        int count = engine->step_requested
                      ? 1
                      : (int)floor((engine->accumulator + 1e-10) / ENGINE_FIXED_SECONDS);
        if (count > ENGINE_STEPS_MAX) {
            count = ENGINE_STEPS_MAX;
        }
        for (int i = 0; i < count; i++) {
            result = fixed_step(engine);
            if (engine->playing) {
                engine->accumulator = fmax(0, engine->accumulator - ENGINE_FIXED_SECONDS);
            }
            engine->step_requested = false;
            if (result < 0) {
                return result;
            }
            if (engine->live && game_behavior_paused(engine->live->behaviors)) {
                break;
            }
        }
        discard_backlog(engine);
    } else if (paused) {
        engine->timing.dropped_seconds += elapsed + engine->accumulator;
        engine->accumulator = 0;
    }
    engine->debug_paused = engine->live &&
                           game_behavior_paused(engine->live->behaviors);
    if (engine->live && engine->timing.simulation_steps == 0) {
        const char *error = game_visibility_update(engine->live->visibility, 0);
        if (error) {
            snprintf(engine->error, sizeof engine->error, "%s", error);
            return FLOW_ERR_NODE;
        }
    }
    engine->timing.alpha = engine->playing && !engine->debug_paused
                             ? (float)(engine->accumulator / ENGINE_FIXED_SECONDS)
                             : 1;
    if (clock_now(&end) != 0) {
        return FLOW_ERR_STATE;
    }
    engine->timing.systems_ms = fmax(
        0,
        engine->timing.systems_ms - engine->timing.physics_ms - engine->timing.events_ms
    );
    engine->timing.behavior_ms = fmax(
        0,
        elapsed_ms(start, end) - engine->timing.systems_ms - engine->timing.physics_ms
    );
    start = end;
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

int engine_frame(engine *engine, double monotonic_time)
{
    if (!engine) {
        return FLOW_ERR_ARGUMENT;
    }
    engine->error[0] = 0;
    int result = advance_frame(engine, monotonic_time);
    if (result < 0 && !engine->error[0]) {
        snprintf(engine->error, sizeof engine->error, "Simulation step failed (%d).", result);
    }
    return result;
}

const char *engine_error(const engine *engine)
{
    return engine->error[0] ? engine->error : NULL;
}

const engine_frame_timing *engine_last_timing(const engine *engine)
{
    return engine ? &engine->timing : NULL;
}

const game_behavior_profile_frame *engine_behavior_profile(const engine *engine)
{
    return engine->live ? game_behavior_profile(engine->live->behaviors) : NULL;
}

void engine_set_behavior_profiling(engine *engine, bool enabled)
{
    if (engine->live) {
        game_behavior_set_profiling(engine->live->behaviors, enabled);
    }
}

bool engine_render_position(const engine *engine, ecs_entity entity, GameTransform *out)
{
    return engine && engine->live &&
           game_physics_interpolate(
               engine->live->physics,
               entity,
               engine->timing.alpha,
               out
           );
}
