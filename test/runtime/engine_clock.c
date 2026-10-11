#include "assets/prototype.h"
#include "authoring/engine_preview.h"
#include "authoring/preview.h"
#include "execution/status.h"
#include "runtime/component_catalog.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

/* The engine's fixed clock: display frames accumulate wall time into 60 Hz
 * steps, overload is capped and reported, pause and Step hold time, time
 * scale changes how often steps run, and presentation interpolates poses
 * between steps without changing them. */

static ecs_world *test_world;
static preview_world *live;
static engine *runtime;
static ecs_entity mover;

static void cleanup()
{
    engine_free(runtime);
    runtime = NULL;
    preview_world_free(live);
    live = NULL;
    ecs_world_free(test_world);
    test_world = NULL;
}

/* Units a second, above the speed under which physics puts bodies to sleep. */
#define SPEED 60

/* A preview engine over one kinematic body moving at SPEED. */
static bool clock_new()
{
    test_world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        assert(!ecs_world_register_component(test_world, REGISTRY_COMPONENTS[i], NULL));
    }
    live = preview_world_new(test_world);
    runtime = authoring_engine_new(test_world, live->runtime);
    prototype_registry registry = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT };
    char error[256] = "";
    prototype *prototype = prototype_load(
        "name \"Mover\"\n"
        "components {\n"
        "    Transform x=0 y=0\n"
        "    Velocity x=60 y=0\n"
        "    Body type=2\n"
        "    Collider radius=1 category=1\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    int spawned = prototype
                    ? prototype_spawn(prototype, test_world, NULL, &mover, error, sizeof error)
                    : -1;
    prototype_free(prototype);
    if (spawned) {
        fprintf(stderr, "engine clock: %s\n", error);
    }
    return !spawned && !engine_set_focus(runtime, ENGINE_FOCUS_VIEWPORT);
}

static void count_render(void *user)
{
    (*(int *)user)++;
}

static int clock_system(ecs_world *, double now, double, void *user, schedule_error *)
{
    double *last = user;
    if (now <= *last || now - *last >= ENGINE_FIXED_SECONDS + 1e-10) {
        *last = -1; /* Out of cadence; never matches again. */
        return 0;
    }
    *last = now;
    return 0;
}

TEST_CASE(display_rates_run_sixty_steps_a_second)
{
    const int rates[] = { 30, 60, 120, 144 };
    for (int r = 0; r < (int)(sizeof rates / sizeof *rates); r++) {
        REQUIRE(clock_new());
        int renders = 0;
        double system_time = 0;
        engine_set_render_callback(runtime, count_render, &renders);
        REQUIRE(!engine_register_system(
            runtime,
            &(schedule_desc){ .name = "clock",
                              .phase = SYSTEM_POST_EVENTS,
                              .run = clock_system,
                              .user = &system_time }
        ));
        for (int i = 0; i <= rates[r]; i++) {
            REQUIRE(engine_frame(runtime, (double)i / rates[r]) == FLOW_OK);
        }
        const engine_frame_timing *timing = engine_last_timing(runtime);
        REQUIRE(timing->simulation_steps == 60 && timing->command_apply_count == 60);
        REQUIRE(timing->dropped_seconds == 0 && renders == rates[r] + 1);
        /* The system ran once per fixed step at each step's simulation time. */
        REQUIRE(fabs(system_time - 1) < 1e-10);
        const Transform *position = Transform_get_const(test_world, mover);
        REQUIRE(fabsf(position->x - SPEED) < .0001f && position->y == 0);
        cleanup();
    }
    return true;
}

TEST_CASE(hitches_and_overload_are_capped_and_reported)
{
    REQUIRE(clock_new());
    REQUIRE(engine_frame(runtime, 10) == FLOW_OK);
    REQUIRE(engine_frame(runtime, 10.1) == FLOW_OK);
    const engine_frame_timing *timing = engine_last_timing(runtime);
    /* A 100 ms display hitch advances all six fixed steps. */
    REQUIRE(timing->steps == 6 && timing->simulation_steps == 6);
    REQUIRE(fabs(timing->simulation_time - .1) < 1e-10 && timing->dropped_seconds == 0);
    /* Overload runs at most eight steps and reports what it discarded. */
    REQUIRE(engine_frame(runtime, 11.1) == FLOW_OK);
    REQUIRE(timing->steps == ENGINE_STEPS_MAX && timing->simulation_steps == 14);
    REQUIRE(fabs(timing->dropped_seconds - (1 - ENGINE_STEPS_MAX * ENGINE_FIXED_SECONDS)) < 1e-9);
    /* Discarded time cannot leak into the next display frame. */
    REQUIRE(engine_frame(runtime, 11.1) == FLOW_OK);
    REQUIRE(timing->steps == 0);
    REQUIRE(engine_frame(runtime, NAN) == FLOW_ERR_ARGUMENT);
    REQUIRE(engine_frame(runtime, INFINITY) == FLOW_ERR_ARGUMENT);
    return true;
}

TEST_CASE(pause_step_resume_and_reset_hold_wall_time)
{
    REQUIRE(clock_new());
    const engine_frame_timing *timing = engine_last_timing(runtime);
    REQUIRE(engine_frame(runtime, 10) == FLOW_OK);
    REQUIRE(engine_frame(runtime, 10 + 2 * ENGINE_FIXED_SECONDS) == FLOW_OK);
    REQUIRE(timing->simulation_steps == 2);
    /* A paused Step advances exactly one tick whatever the idle wall time. */
    REQUIRE(!engine_set_playing(runtime, false));
    REQUIRE(engine_frame(runtime, 50) == FLOW_OK);
    REQUIRE(engine_step(runtime) == FLOW_OK);
    REQUIRE(engine_frame(runtime, 100) == FLOW_OK);
    REQUIRE(engine_frame(runtime, 101) == FLOW_OK);
    REQUIRE(timing->simulation_steps == 3 && !engine_is_playing(runtime));
    /* Resuming rebases the wall clock instead of catching up. */
    REQUIRE(!engine_set_playing(runtime, true));
    REQUIRE(engine_frame(runtime, 200) == FLOW_OK);
    REQUIRE(engine_frame(runtime, 200 + ENGINE_FIXED_SECONDS) == FLOW_OK);
    REQUIRE(timing->simulation_steps == 4);
    /* Restart resets simulation time, overload accounting and the baseline. */
    engine_reset(runtime);
    REQUIRE(engine_frame(runtime, 500) == FLOW_OK);
    REQUIRE(timing->simulation_time == 0 && timing->dropped_seconds == 0 && timing->steps == 0);
    return true;
}

/* Slow motion (Unity's Time.timeScale, Unreal's slomo) scales the time each
 * frame adds; fixed steps keep their length. */
TEST_CASE(time_scale_changes_how_often_steps_run)
{
    REQUIRE(clock_new());
    const engine_frame_timing *timing = engine_last_timing(runtime);
    REQUIRE(engine_time_scale(runtime) == 1);
    engine_set_time_scale(runtime, .25);
    REQUIRE(engine_frame(runtime, 10) == FLOW_OK);
    REQUIRE(engine_frame(runtime, 10 + 8 * ENGINE_FIXED_SECONDS) == FLOW_OK);
    REQUIRE(timing->simulation_steps == 2);
    REQUIRE(fabs(timing->simulation_time - 2 * ENGINE_FIXED_SECONDS) < 1e-10);
    engine_set_time_scale(runtime, 2);
    REQUIRE(engine_frame(runtime, 10 + 9 * ENGINE_FIXED_SECONDS) == FLOW_OK);
    REQUIRE(timing->steps == 2 && timing->simulation_steps == 4);
    /* Step advances exactly one tick at any speed. */
    engine_set_time_scale(runtime, .1);
    REQUIRE(!engine_set_playing(runtime, false));
    REQUIRE(engine_step(runtime) == FLOW_OK);
    REQUIRE(engine_frame(runtime, 20) == FLOW_OK);
    REQUIRE(timing->simulation_steps == 5);
    return true;
}

TEST_CASE(display_frames_interpolate_without_moving_bodies)
{
    REQUIRE(clock_new());
    REQUIRE(engine_frame(runtime, 0) == FLOW_OK);
    REQUIRE(engine_frame(runtime, .1) == FLOW_OK);
    float solved = Transform_get_const(test_world, mover)->x;
    REQUIRE(solved > 0);
    REQUIRE(engine_frame(runtime, .1 + ENGINE_FIXED_SECONDS * .5) == FLOW_OK);
    CF_V2 shown = engine_render_transform(runtime, test_world, mover).position;
    REQUIRE(fabs(shown.x - (solved - SPEED * ENGINE_FIXED_SECONDS * .5)) < .0001f);
    REQUIRE(Transform_get_const(test_world, mover)->x == solved);
    REQUIRE(engine_last_timing(runtime)->steps == 0);
    /* Pause shows the latest solved pose at once. */
    REQUIRE(!engine_set_playing(runtime, false));
    REQUIRE(engine_render_transform(runtime, test_world, mover).position.x == solved);
    return true;
}

/* A headless engine without input, physics or rendering still steps. */
TEST_CASE(an_engine_without_optional_modules_steps_headlessly)
{
    test_world = ecs_world_new();
    runtime = engine_new(test_world, &(engine_modules){ 0 });
    REQUIRE(!engine_set_playing(runtime, false));
    REQUIRE(engine_step(runtime) == FLOW_OK && engine_frame(runtime, 0) == FLOW_OK);
    REQUIRE(engine_last_timing(runtime)->simulation_steps == 1);
    /* An uninstalled input service rejects host input. */
    REQUIRE(engine_sample_movement(runtime, 1, 0) == FLOW_ERR_STATE);
    return true;
}

void test_engine_clock()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(display_rates_run_sixty_steps_a_second);
    RUN_TEST_CASE(hitches_and_overload_are_capped_and_reported);
    RUN_TEST_CASE(pause_step_resume_and_reset_hold_wall_time);
    RUN_TEST_CASE(time_scale_changes_how_often_steps_run);
    RUN_TEST_CASE(display_frames_interpolate_without_moving_bodies);
    RUN_TEST_CASE(an_engine_without_optional_modules_steps_headlessly);
    pu_clear_setup();
}
