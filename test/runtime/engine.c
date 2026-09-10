#include "support/array.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <time.h>

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    game_prototype_preview *preview;
    ecs_entity entity;
} clock_fixture;

static node *clock_event(graph *g, const char *name)
{
    node *event = graph_add_node(g, registry_node_by_name("Event"));
    assert(event && graph_set_event_name(g, event, name) == 0);
    return event;
}

static node *clock_log(graph *g, node *from, const char *message)
{
    node *log = graph_add_node(g, registry_node_by_name("Print String"));
    assert(
        log &&
        graph_set_pin_default(g, log, 0, 1, &(pin_value){ .type = PIN_STR, .str = message }) == 0
    );
    assert(graph_connect_exec(g, from, 0, log, 0, 0) == 0);
    return log;
}

static behavior_asset *clock_behavior(bool breakpoint)
{
    behavior_asset *asset = behavior_asset_new("Clock");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *g = asset->graph;
    node *input = clock_event(g, "Input");
    assert(
        graph_add_pin(g, input, true, &(pin_spec){ .name = "Direction", .type = PIN_STRUCT, .type_name = "CF_V2" }) ==
        1
    );
    node *velocity = graph_add_node(g, registry_node_by_name("Set_GameVelocity"));
    assert(velocity && graph_connect_exec(g, input, 0, velocity, 0, 0) == 0);
    assert(graph_connect_data(g, input, 1, velocity, 1) == 0);
    clock_log(g, velocity, "input");
    node *tick = clock_log(g, clock_event(g, "Tick"), "tick");
    assert(graph_set_breakpoint(g, tick, breakpoint) == 0);
    clock_log(g, clock_event(g, "Fire"), "fire");
    clock_log(g, clock_event(g, "Dash"), "dash");
    node *begin = clock_event(g, "Begin");
    node *delay = graph_add_node(g, registry_node_by_name("Delay"));
    assert(
        delay &&
        graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 0.1 }) == 0
    );
    assert(graph_connect_exec(g, begin, 0, delay, 0, 0) == 0);
    clock_log(g, delay, "ready");
    return asset;
}

static int clock_attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    return game_behavior_attach(user, entity, name);
}

static clock_fixture clock_new(bool preview, bool breakpoint)
{
    clock_fixture f = { 0 };
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char error[256];
    game_prototype *prototype = game_prototype_load(
        "{\"name\":\"Clock\",\"behaviors\":[\"Clock\"],\"components\":{"
        "\"GameTransform\":{\"x\":0,\"y\":0},\"GameVelocity\":{\"x\":0,\"y\":0}"
        ","
        "\"GameBody\":{\"type\":2},\"GameCollider\":{\"radius\":1,\"category\":"
        "1},"
        "\"GameInput\":{\"move\":{\"x\":0,\"y\":0}},"
        "\"GameAim\":{\"direction\":{\"x\":1,\"y\":0},\"firing\":false}}}",
        &registry,
        error,
        sizeof error
    );
    assert(prototype);
    behavior_asset *asset = clock_behavior(breakpoint);
    if (preview) {
        f.preview = game_prototype_preview_new(&registry);
        assert(game_prototype_preview_register_behavior(f.preview, "Clock", asset) == 0);
        assert(game_prototype_preview_launch(f.preview, prototype, error, sizeof error) == 0);
        behavior_asset_free(asset);
        f.world = game_prototype_preview_world(f.preview);
        f.runtime = game_prototype_preview_runtime(f.preview);
        f.entity = game_prototype_preview_entity(f.preview);
    } else {
        f.world = ecs_world_new();
        for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
            assert(
                ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK
            );
        }
        f.live = game_live_world_new(f.world);
        f.runtime = engine_new(f.world, f.live);
        assert(game_live_add_definition(f.live, game_live_definition_new("Clock", asset)) == 0);
        assert(
            game_prototype_spawn(
                prototype,
                f.world,
                &(game_prototype_spawn_hooks){ clock_attach, f.live->behaviors },
                &f.entity,
                error,
                sizeof error
            ) == 0
        );
    }
    game_prototype_free(prototype);
    assert(engine_set_focus(f.runtime, ENGINE_FOCUS_VIEWPORT) == 0);
    return f;
}

static game_behavior_world *clock_behaviors(clock_fixture *f)
{
    return f->preview ? game_prototype_preview_behaviors(f->preview) : f->live->behaviors;
}

static int clock_logs(clock_fixture *f, const char *message, ecs_entity entity)
{
    game_behavior_world *behaviors = clock_behaviors(f);
    int count = 0;
    for (int i = 0; i < game_behavior_log_count(behaviors); i++) {
        game_behavior_log_info info;
        assert(game_behavior_log_info_at(behaviors, i, &info) == 0);
        count += !strcmp(info.message, message) &&
                 (!entity.slot ||
                  ecs_entity_to_u64(info.entity) == ecs_entity_to_u64(entity));
    }
    return count;
}

static void clock_free(clock_fixture *f)
{
    if (f->preview) {
        game_prototype_preview_free(f->preview);
        return;
    }
    engine_free(f->runtime);
    game_live_world_free(f->live);
    ecs_world_free(f->world);
}

static void clock_render(void *user)
{
    (*(int *)user)++;
}

static void clock_system(ecs_world *, double now, void *user)
{
    double *last = user;
    assert(now > *last && now - *last < ENGINE_FIXED_SECONDS + 1e-10);
    *last = now;
}

static void cadence_checks()
{
    const int rates[] = { 30, 60, 120, 144 };
    for (int preview = 0; preview < 2; preview++) {
        for (int r = 0; r < (int)(sizeof rates / sizeof *rates); r++) {
            clock_fixture f = clock_new(preview, false);
            int renders = 0;
            double system_time = 0;
            engine_set_render_callback(f.runtime, clock_render, &renders);
            assert(engine_add_system(f.runtime, clock_system, &system_time) == 0);
            assert(engine_sample_movement(f.runtime, 1, 0) == FLOW_OK);
            assert(engine_sample_aim(f.runtime, (CF_V2){ 1, 1 }, true, false) == FLOW_OK);
            for (int i = 0; i <= rates[r]; i++) {
                assert(engine_frame(f.runtime, (double)i / rates[r]) == FLOW_OK);
            }
            const engine_frame_timing *timing = engine_last_timing(f.runtime);
            const GameTransform *position = GAME_GameTransform_get_const(f.world, f.entity);
            const GameAim *aim = GAME_GameAim_get_const(f.world, f.entity);
            test_check(
                timing->simulation_steps == 60 && timing->command_apply_count == 60 &&
                    timing->dropped_seconds == 0 && fabs(system_time - 1) < 1e-10 &&
                    renders == rates[r] + 1 && clock_logs(&f, "tick", f.entity) == 60 &&
                    clock_logs(&f, "input", f.entity) == 60 &&
                    clock_logs(&f, "ready", f.entity) == 1 &&
                    fabsf(position->x - 1) < 0.00001f && position->y == 0 &&
                    aim->firing && fabsf(aim->direction.x - sqrtf(.5f)) < 0.00001f,
                "scene and prototype engines preserve velocity, held input, "
                "delays and 60 steps at 30/60/120/144 Hz"
            );
            clock_free(&f);
        }
    }
}

static void spawn_cadence_checks(int count)
{
    clock_fixture f = clock_new(false, false);
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char error[256];
    game_prototype *prototype = game_prototype_load(
        "{\"name\":\"ClockChild\",\"behaviors\":[\"ClockChild\"],"
        "\"components\":{\"GameTransform\":{\"x\":0,\"y\":0}}}",
        &registry,
        error,
        sizeof error
    );
    assert(prototype && game_entities_register(f.live->entities, prototype) == 0);
    game_prototype_free(prototype);
    behavior_asset *child = behavior_asset_new("ClockChild");
    assert(behavior_asset_set_owner_type(child, "ecs.entity") == 0);
    clock_log(child->graph, clock_event(child->graph, "Begin"), "child ready");
    assert(game_live_add_definition(f.live, game_live_definition_new("ClockChild", child)) == 0);

    behavior_asset *asset = behavior_asset_new("ClockSpawner");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *g = asset->graph;
    node *tick = clock_event(g, "Tick");
    node *loop = graph_add_node(g, registry_node_by_name("ForLoop"));
    node *spawn = graph_add_node(g, registry_node_by_name("Spawn Prototype"));
    node *make = graph_add_node(g, registry_node_by_name("Make_GameTransform"));
    node *initialize = graph_add_node(g, registry_node_by_name("SetEntity_GameTransform"));
    assert(loop && spawn && make && initialize);
    assert(graph_set_pin_default(g, loop, 0, 1, &(pin_value){ .type = PIN_I32, .i = 0 }) == 0);
    assert(
        graph_set_pin_default(g, loop, 0, 2, &(pin_value){ .type = PIN_I32, .i = count - 1 }) == 0
    );
    assert(
        graph_set_pin_default(g, spawn, 0, 1, &(pin_value){ .type = PIN_NAME, .str = "ClockChild" }) == 0
    );
    assert(graph_set_pin_default(g, make, 0, 0, &(pin_value){ .type = PIN_F32, .f = 73 }) == 0);
    assert(graph_connect_exec(g, tick, 0, loop, 0, 0) == 0);
    assert(graph_connect_exec(g, loop, 0, spawn, 0, 0) == 0);
    assert(graph_connect_exec(g, spawn, 0, initialize, 0, 0) == 0);
    assert(graph_connect_data(g, spawn, 1, initialize, 1) == 0);
    assert(graph_connect_data(g, make, 0, initialize, 2) == 0);
    assert(game_live_add_definition(f.live, game_live_definition_new("ClockSpawner", asset)) == 0);
    assert(game_behavior_attach(f.live->behaviors, f.entity, "ClockSpawner") == 0);
    assert(engine_sample_movement(f.runtime, 1, 0) == FLOW_OK);
    assert(engine_frame(f.runtime, 0) == FLOW_OK);
    ecs_entity *children = NULL;
    double system_time = 0;
    assert(engine_add_system(f.runtime, clock_system, &system_time) == 0);
    const int steps[] = { 1, 4, 6 };
    int frames = count == 8 ? (int)(sizeof steps / sizeof *steps) : 1;
    for (int frame = 0; frame < frames; frame++) {
        int step = steps[frame];
        assert(engine_frame(f.runtime, step * ENGINE_FIXED_SECONDS) == FLOW_OK);
        game_entities_copy_live(f.live->entities, &children);
        const engine_frame_timing *timing = engine_last_timing(f.runtime);
        const GameTransform *position = GAME_GameTransform_get_const(f.world, f.entity);
        test_check(
            acount(children) == step * count &&
                clock_logs(&f, "child ready", (ecs_entity){ 0 }) == step * count &&
                timing->simulation_steps == (uint64_t)step &&
                timing->command_apply_count == (uint32_t)step &&
                fabs(position->x - step * ENGINE_FIXED_SECONDS) < 1e-6,
            "sequential spawn initializers and physics finish in the same "
            "fixed tick without losing movement"
        );
        for (int i = 0; i < acount(children); i++) {
            assert(GAME_GameTransform_get_const(f.world, children[i])->x == 73);
        }
    }
    afree(children);
    clock_free(&f);
}

static void debugger_clock_checks()
{
    for (int single = 0; single < 2; single++) {
        clock_fixture f = clock_new(false, true);
        behavior_instance *instance = game_behavior_instance(f.live->behaviors, f.entity, "Clock");
        behavior_instance_set_breakpoints_enabled(instance, true);
        assert(engine_frame(f.runtime, 0) == FLOW_OK);
        if (single) {
            assert(engine_set_playing(f.runtime, false) == FLOW_OK);
            assert(engine_step(f.runtime) == FLOW_OK);
        }
        assert(engine_frame(f.runtime, ENGINE_FIXED_SECONDS) == FLOW_OK);
        flow_run *held = NULL;
        for (int i = 0; i < acount(instance->activations); i++) {
            if (flow_run_status(instance->activations[i]) == FLOW_PAUSED) {
                held = instance->activations[i];
            }
        }
        assert(held && engine_last_timing(f.runtime)->command_apply_count == 0);
        assert(engine_frame(f.runtime, 10) == FLOW_OK);
        assert(engine_last_timing(f.runtime)->simulation_steps == 1);
        assert(flow_run_resume(held) >= 0);
        assert(engine_frame(f.runtime, 10) == FLOW_OK);
        test_check(
            engine_last_timing(f.runtime)->simulation_steps == 1 &&
                engine_last_timing(f.runtime)->command_apply_count == 1 &&
                engine_last_timing(f.runtime)->steps == 0 &&
                clock_logs(&f, "tick", f.entity) == 1,
            "debugger Resume finishes the held step at its original time, "
            "including paused Step"
        );
        behavior_instance_set_breakpoints_enabled(instance, false);
        if (single) {
            assert(engine_step(f.runtime) == FLOW_OK);
        }
        assert(engine_frame(f.runtime, 10 + ENGINE_FIXED_SECONDS) == FLOW_OK);
        test_check(
            engine_last_timing(f.runtime)->simulation_steps == 2 &&
                engine_last_timing(f.runtime)->command_apply_count == 2 &&
                clock_logs(&f, "tick", f.entity) == 2,
            "the next fixed tick runs a new Tick after debugger continuation"
        );
        clock_free(&f);
    }
}

static void presentation_checks()
{
    clock_fixture f = clock_new(false, false);
    assert(engine_sample_movement(f.runtime, 1, 0) == FLOW_OK);
    assert(engine_frame(f.runtime, 0) == FLOW_OK);
    assert(engine_frame(f.runtime, .1) == FLOW_OK);
    float solved = GAME_GameTransform_get_const(f.world, f.entity)->x;
    assert(engine_frame(f.runtime, .1 + ENGINE_FIXED_SECONDS * .5) == FLOW_OK);
    GameTransform displayed;
    test_check(
        engine_render_position(f.runtime, f.entity, &displayed) &&
            fabs(displayed.x - (solved - ENGINE_FIXED_SECONDS * .5)) < .00001f &&
            GAME_GameTransform_get_const(f.world, f.entity)->x == solved &&
            engine_last_timing(f.runtime)->steps == 0,
        "fractional display frames interpolate physics poses without changing "
        "authoritative Transform"
    );
    assert(engine_set_playing(f.runtime, false) == 0);
    test_check(
        engine_render_position(f.runtime, f.entity, &displayed) && displayed.x == solved,
        "Pause displays the latest solved pose immediately"
    );
    clock_free(&f);
}

static void attachment_clock_checks()
{
    for (int preview = 0; preview < 2; preview++) {
        clock_fixture f = clock_new(preview, false);
        for (int i = 0; i <= 60; i++) {
            assert(engine_frame(f.runtime, i * ENGINE_FIXED_SECONDS) == FLOW_OK);
        }
        ecs_entity late = ecs_world_create_entity(f.world);
        assert(game_behavior_attach(clock_behaviors(&f), late, "Clock") == 0);
        for (int i = 61; i <= 65; i++) {
            assert(engine_frame(f.runtime, i * ENGINE_FIXED_SECONDS) == FLOW_OK);
        }
        test_check(clock_logs(&f, "ready", late) == 0, "a late attachment schedules Begin Delay relative to the current simulation time");
        assert(engine_frame(f.runtime, 66 * ENGINE_FIXED_SECONDS) == FLOW_OK);
        test_check(clock_logs(&f, "ready", late) == 1, "a late attachment wakes at the same fixed deadline in scene and preview");
        clock_free(&f);
    }
}

static void accumulator_checks()
{
    clock_fixture f = clock_new(false, false);
    assert(engine_frame(f.runtime, 10) == FLOW_OK);
    assert(engine_frame(f.runtime, 10.1) == FLOW_OK);
    const engine_frame_timing *timing = engine_last_timing(f.runtime);
    test_check(
        timing->steps == 6 && timing->simulation_steps == 6 &&
            fabs(timing->simulation_time - .1) < 1e-10 && timing->dropped_seconds == 0,
        "a 100 ms display hitch advances all six fixed steps"
    );
    assert(engine_frame(f.runtime, 11.1) == FLOW_OK);
    test_check(
        timing->steps == 8 && timing->simulation_steps == 14 &&
            fabs(timing->dropped_seconds - (1 - 8 * ENGINE_FIXED_SECONDS)) < 1e-9,
        "overload runs at most eight steps and reports capped plus discarded "
        "time"
    );
    assert(engine_frame(f.runtime, 11.1) == FLOW_OK);
    test_check(timing->steps == 0, "discarded overload cannot leak into the following display");
    assert(engine_frame(f.runtime, NAN) == FLOW_ERR_ARGUMENT);
    assert(engine_frame(f.runtime, INFINITY) == FLOW_ERR_ARGUMENT);
    assert(engine_set_playing(f.runtime, false) == 0);
    assert(engine_frame(f.runtime, 50) == FLOW_OK);
    assert(engine_step(f.runtime) == FLOW_OK);
    assert(engine_frame(f.runtime, 100) == FLOW_OK);
    assert(engine_frame(f.runtime, 101) == FLOW_OK);
    test_check(timing->simulation_steps == 15 && !engine_is_playing(f.runtime), "paused Step advances exactly one fixed tick regardless of idle wall time");
    assert(engine_set_playing(f.runtime, true) == 0);
    assert(engine_frame(f.runtime, 200) == FLOW_OK);
    assert(engine_frame(f.runtime, 200 + ENGINE_FIXED_SECONDS) == FLOW_OK);
    test_check(timing->simulation_steps == 16, "resume rebases the wall clock without catching up paused time");
    engine_reset(f.runtime);
    assert(engine_frame(f.runtime, 500) == FLOW_OK);
    test_check(
        timing->simulation_time == 0 && timing->dropped_seconds == 0 &&
            timing->steps == 0,
        "restart resets simulation time, overload accounting and wall baseline"
    );
    clock_free(&f);
}

static void action_checks()
{
    clock_fixture f = clock_new(false, false);
    ecs_entity observer = ecs_world_create_entity(f.world);
    assert(game_behavior_attach(f.live->behaviors, observer, "Clock") == 0);
    assert(engine_frame(f.runtime, 0) == FLOW_OK);
    flow_error error = { 0 };
    assert(engine_dispatch_input(f.runtime, "Jump", 1, 1, &error) == FLOW_ERR_UNKNOWN_EVENT);
    assert(engine_dispatch_input(f.runtime, "Fire", 1, 1, NULL) == 1);
    assert(engine_dispatch_input(f.runtime, "Fire", 0, 0, NULL) == 1);
    assert(engine_dispatch_input(f.runtime, "Fire", 1, 1, NULL) == 1);
    assert(engine_dispatch_input(f.runtime, "Dash", 1, 1, NULL) == 1);
    assert(engine_frame(f.runtime, .005) == FLOW_OK);
    test_check(clock_logs(&f, "fire", (ecs_entity){ 0 }) == 0, "button transitions wait for a simulation tick");
    assert(engine_frame(f.runtime, .1) == FLOW_OK);
    test_check(
        clock_logs(&f, "fire", f.entity) == 2 && clock_logs(&f, "dash", f.entity) == 1 &&
            clock_logs(&f, "fire", observer) == 0 &&
            clock_logs(&f, "dash", observer) == 0,
        "quick press/release/press survives catch-up and only GameInput owners "
        "receive Fire/Dash"
    );
    assert(engine_dispatch_input(f.runtime, "Fire", 1, 1, NULL) == 0);
    assert(engine_sample_movement(f.runtime, 1, 0) == FLOW_OK);
    assert(engine_frame(f.runtime, .2) == FLOW_OK);
    float before = GAME_GameTransform_get_const(f.world, f.entity)->x;
    assert(ecs_world_record_remove(f.world, f.entity, GAME_COMPONENT_GameInput.id, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(engine_frame(f.runtime, .3) == FLOW_OK);
    test_check(
        GAME_GameVelocity_get_const(f.world, f.entity)->x == 0 &&
            GAME_GameTransform_get_const(f.world, f.entity)->x == before,
        "losing local input ownership sends one zero movement release before "
        "physics"
    );
    assert(engine_route_escape(f.runtime) == 0);
    assert(engine_dispatch_input(f.runtime, "Dash", 1, 1, NULL) == FLOW_ERR_STATE);
    clock_free(&f);
}

typedef struct
{
    game_entities *entities;
    ecs_entity target;
    bool recursive;
    int calls;
} reaction_fixture;

static int clock_reaction(node *, flow_context *ctx)
{
    reaction_fixture *reaction = flow_context_service(ctx, "test.reaction");
    assert(reaction);
    reaction->calls++;
    if (!reaction->recursive && reaction->calls == 2) {
        game_entities_request_destroy(reaction->entities, reaction->target);
        return flow_context_continue(ctx, 0);
    }
    pin_value other = { .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .u64 = ecs_entity_to_u64(reaction->target) };
    int result = game_entities_request_event(
        reaction->entities,
        reaction->target,
        reaction->recursive ? "Contact Begin" : "Damage",
        reaction->recursive ? (flow_inputs){ &other, 1, NULL } : (flow_inputs){ 0 }
    );
    return result < 0 ? result : flow_context_continue(ctx, 0);
}

static pin_spec reaction_exec[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC } };
static node_desc reaction_desc = {
    .name = "Clock Reaction",
    .input_count = 1,
    .output_count = 1,
    .inputs = reaction_exec,
    .outputs = reaction_exec,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = clock_reaction,
};

static void reaction_checks(bool recursive)
{
    clock_fixture f = clock_new(false, false);
    GameCollider *collider = GAME_GameCollider_get(f.world, f.entity);
    collider->mask = 2;
    collider->sensor = true;
    GAME_GameBody_get(f.world, f.entity)->type = GAME_BODY_DYNAMIC;
    ecs_entity other = ecs_world_create_entity(f.world);
    assert(GAME_GameTransform_add(f.world, other, 0) == ECS_OK);
    assert(GAME_GameCollider_add(f.world, other, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    *GAME_GameCollider_get(f.world, other) = (GameCollider){ .radius = 1,
                                                             .category = 2,
                                                             .mask = 1 };
    behavior_asset *asset = behavior_asset_new("Reactions");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    const char *events[] = { "Contact Begin", "Damage" };
    for (int i = 0; i < 2; i++) {
        node *event = clock_event(asset->graph, events[i]);
        if (!i) {
            assert(
                graph_add_pin(
                    asset->graph,
                    event,
                    true,
                    &(pin_spec){ .name = "Other", .type = PIN_HANDLE, .type_name = "ecs.entity" }
                ) == 1
            );
        }
        node *reaction = graph_add_node(asset->graph, &reaction_desc);
        assert(graph_connect_exec(asset->graph, event, 0, reaction, 0, 0) == 0);
    }
    assert(game_behavior_register_asset(f.live->behaviors, "Reactions", asset) == 0);
    assert(game_behavior_attach(f.live->behaviors, f.entity, "Reactions") == 0);
    reaction_fixture reaction = { f.live->entities, f.entity, recursive, 0 };
    assert(
        behavior_instance_bind_service(
            game_behavior_instance(f.live->behaviors, f.entity, "Reactions"),
            "test.reaction",
            &reaction
        ) == 0
    );
    assert(engine_frame(f.runtime, 0) == FLOW_OK);
    assert(engine_frame(f.runtime, ENGINE_FIXED_SECONDS) == FLOW_OK);
    if (recursive) {
        test_check(
            reaction.calls == ENGINE_EVENT_BATCHES_MAX &&
                engine_last_timing(f.runtime)->event_backlog == 1 &&
                engine_last_timing(f.runtime)->steps == 1,
            "recursive contact handlers are bounded and leave observable "
            "backlog for the next tick"
        );
    } else {
        test_check(
            reaction.calls == 2 && !ecs_entity_is_alive(f.world, f.entity) &&
                engine_last_timing(f.runtime)->event_backlog == 0,
            "physics Contact, queued Damage and entity destruction finish in "
            "the same fixed tick"
        );
    }
    clock_free(&f);
    behavior_asset_free(asset);
}

static game_behavior_profile_row profile_row(
    const game_behavior_profile_frame *frame,
    const char *behavior,
    const char *event
)
{
    for (int i = 0; i < frame->count; i++) {
        if (!strcmp(frame->rows[i].behavior, behavior) &&
            !strcmp(frame->rows[i].event, event)) {
            return frame->rows[i];
        }
    }
    return (game_behavior_profile_row){ 0 };
}

static uint64_t profile_clock()
{
    struct timespec time;
    assert(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

typedef struct
{
    game_behavior_world *world;
    ecs_entity target;
} profile_fixture;

static int profile_dispatch(node *, flow_context *ctx)
{
    profile_fixture *f = flow_context_service(ctx, "test.profile");
    assert(game_behavior_dispatch(f->world, f->target, "Work", (flow_inputs){ 0 }, NULL) == 1);
    return flow_context_continue(ctx, 0);
}

static int profile_work(node *, flow_context *ctx)
{
    volatile uint64_t sum = 0;
    for (uint64_t i = 0; i < 200000; i++) {
        sum = sum * 17 + i;
    }
    assert(sum != 0);
    return flow_context_continue(ctx, 0);
}

static node_desc profile_dispatch_desc = {
    .name = "Profile Dispatch",
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = profile_dispatch,
    .input_count = 1,
    .output_count = 1,
    .inputs = reaction_exec,
    .outputs = reaction_exec,
};
static node_desc profile_work_desc = {
    .name = "Profile Work",
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = profile_work,
    .input_count = 1,
    .output_count = 1,
    .inputs = reaction_exec,
    .outputs = reaction_exec,
};

static void profile_checks()
{
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    ecs_entity parent = ecs_world_create_entity(ecs),
               child = ecs_world_create_entity(ecs);
    behavior_asset *outer = behavior_asset_new("Profile Parent");
    node *call = graph_add_node(outer->graph, &profile_dispatch_desc);
    assert(
        graph_connect_exec(outer->graph, clock_event(outer->graph, "Outer"), 0, call, 0, 0) == 0
    );
    behavior_asset *inner = behavior_asset_new("Profile Child");
    graph *function = behavior_asset_add_function(inner, "Nested Work", 0);
    node *work = graph_add_node(function, &profile_work_desc);
    assert(
        graph_connect_exec(function, graph_node_by_id(function, function->entry_id), 0, work, 0, 0) == 0
    );
    assert(
        graph_connect_exec(
            function,
            work,
            0,
            graph_node_by_id(function, function->return_id),
            0,
            0
        ) == 0
    );
    node *nested = behavior_asset_add_function_call_node_to_graph(inner, inner->graph, function);
    assert(
        graph_connect_exec(inner->graph, clock_event(inner->graph, "Work"), 0, nested, 0, 0) == 0
    );
    node *delay = graph_add_node(inner->graph, registry_node_by_name("Delay"));
    assert(
        graph_set_pin_default(inner->graph, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .1 }) == 0
    );
    assert(
        graph_connect_exec(inner->graph, clock_event(inner->graph, "Slow"), 0, delay, 0, 0) == 0
    );
    assert(graph_connect_exec(inner->graph, delay, 0, nested, 0, 0) == 0);
    assert(game_behavior_register_asset(world, outer->name, outer) == 0);
    assert(game_behavior_register_asset(world, inner->name, inner) == 0);
    assert(game_behavior_attach(world, parent, outer->name) == 0);
    assert(game_behavior_attach(world, child, inner->name) == 0);
    profile_fixture context = { world, child };
    assert(
        behavior_instance_bind_service(
            game_behavior_instance(world, parent, outer->name),
            "test.profile",
            &context
        ) == 0
    );
    game_behavior_profile_begin(world);
    uint64_t start = profile_clock();
    assert(game_behavior_dispatch(world, parent, "Outer", (flow_inputs){ 0 }, NULL) == 1);
    double wall_ms = (profile_clock() - start) / 1000000.0;
    const game_behavior_profile_frame *frame = game_behavior_profile(world);
    game_behavior_profile_row outer_row = profile_row(frame, outer->name, "Outer");
    game_behavior_profile_row work_row = profile_row(frame, inner->name, "Work");
    test_check(
        frame->count == 2 && outer_row.starts == 1 && work_row.starts == 1 &&
            !outer_row.resumes && !work_row.resumes && work_row.cpu_ms > 0 &&
            frame->cpu_ms <= wall_ms,
        "nested functions stay in their event while reentrant events are "
        "excluded from the caller's CPU time"
    );
    assert(game_behavior_dispatch(world, child, "Slow", (flow_inputs){ 0 }, NULL) == 1);
    game_behavior_profile_begin(world);
    assert(game_behavior_pump(world, .05) == FLOW_OK);
    test_check(game_behavior_profile(world)->count == 0, "waiting events accrue no execution time or resumes before their deadline");
    game_behavior_set_time(world, .1);
    assert(game_behavior_dispatch(world, child, "Work", (flow_inputs){ 0 }, NULL) == 1);
    frame = game_behavior_profile(world);
    game_behavior_profile_row slow = profile_row(frame, inner->name, "Slow");
    work_row = profile_row(frame, inner->name, "Work");
    test_check(
        slow.starts == 0 && slow.resumes == 1 && slow.cpu_ms > 0 &&
            work_row.starts == 1 && !work_row.resumes,
        "dispatching Work retains Slow attribution when the scheduler first "
        "resumes the older activation"
    );
    ecs_entity second = ecs_world_create_entity(ecs);
    assert(game_behavior_attach(world, second, inner->name) == 0);
    assert(game_behavior_dispatch(world, second, "Work", (flow_inputs){ 0 }, NULL) == 1);
    work_row = profile_row(game_behavior_profile(world), inner->name, "Work");
    test_check(work_row.starts == 2, "instances aggregate into one behavior and event row");
    assert(game_behavior_destroy_entity(world, child) == ECS_OK);
    assert(game_behavior_destroy_entity(world, second) == ECS_OK);
    behavior_asset *released;
    assert(game_behavior_unregister_asset(world, inner->name, &released) == 0 && released == inner);
    behavior_asset_free(inner);
    test_check(
        profile_row(game_behavior_profile(world), "Profile Child", "Work").starts == 2,
        "captured profile labels survive entity destruction and asset removal"
    );
    game_behavior_profile_begin(world);
    test_check(
        game_behavior_profile(world)->count == 0 &&
            game_behavior_profile(world)->cpu_ms == 0,
        "a new display frame clears event totals without retaining earlier time"
    );
    game_behavior_world_free(world);
    behavior_asset_free(outer);
    ecs_world_free(ecs);

    clock_fixture f = clock_new(false, false);
    assert(engine_frame(f.runtime, 0) == FLOW_OK);
    assert(engine_frame(f.runtime, 3 * ENGINE_FIXED_SECONDS) == FLOW_OK);
    test_check(
        profile_row(engine_behavior_profile(f.runtime), "Clock", "Tick").starts == 3,
        "engine profile includes all fixed catch-up ticks in one display frame"
    );
    assert(engine_set_playing(f.runtime, false) == 0);
    assert(engine_frame(f.runtime, 1) == FLOW_OK);
    test_check(engine_behavior_profile(f.runtime)->count == 0, "paused frames contain no stale behavior execution");
    clock_free(&f);
}

void run_engine_checks(void)
{
    cadence_checks();
    spawn_cadence_checks(8);
    spawn_cadence_checks(72);
    debugger_clock_checks();
    presentation_checks();
    attachment_clock_checks();
    accumulator_checks();
    action_checks();
    reaction_checks(false);
    reaction_checks(true);
    profile_checks();
}

static int g_parallel_calls;
static void reverse_parallel_for(int count, void (*worker)(int, void *), void *user)
{
    g_parallel_calls++;
    for (int i = count - 1; i >= 0; i--) {
        worker(i, user);
    }
}

void run_parallel_checks(void)
{
    test_check(
        GAME_NODE_game_component_probe.access &&
            GAME_NODE_game_component_probe.access->read_count == 1 &&
            GAME_NODE_game_component_probe.access->reads[0] ==
                GAME_COMPONENT_GameHealth.id,
        "node descriptors declare component reads, writes, and scope "
        "flags"
    );
    test_check(
        GAME_NODE_Set_GameHealth.access &&
            (GAME_NODE_Set_GameHealth.access->flags & NODE_EFFECT_ENTITY_SCOPED),
        "generated component writes are entity-scoped"
    );
    test_check(
        GAME_NODE_Add_GameHealth.access &&
            (GAME_NODE_Add_GameHealth.access->flags & NODE_EFFECT_STRUCTURAL),
        "structural component effects are declared for deferred recording"
    );

    behavior_asset *safe = behavior_asset_new("SafeEffects");
    graph_add_node(safe->graph, &GAME_NODE_game_component_probe);
    compile_report safe_report;
    compile_report_init(&safe_report);
    behavior_asset_compile(safe, &safe_report);
    const compile_effects *safe_effects = compile_report_effects(&safe_report);
    test_check(
        safe_effects && safe_effects->parallel_eligible && !safe_effects->all_writes &&
            safe_effects->topology_revision == graph_topology_revision(safe->graph),
        "compile caches a per-entry effect mask against topology "
        "revision"
    );

    behavior_asset *unsafe = behavior_asset_new("ConservativeEffects");
    graph_add_node(unsafe->graph, registry_node_by_name("Print String"));
    compile_report unsafe_report;
    compile_report_init(&unsafe_report);
    behavior_asset_compile(unsafe, &unsafe_report);
    const compile_effects *unsafe_effects = compile_report_effects(&unsafe_report);
    test_check(
        unsafe_effects && unsafe_effects->all_writes && !unsafe_effects->parallel_eligible,
        "an undeclared node conservatively writes every component"
    );
    test_check(
        compile_effects_conflict(safe_effects, safe_effects, 1) &&
            !compile_effects_conflict(safe_effects, safe_effects, 0) &&
            compile_effects_conflict(safe_effects, unsafe_effects, 0),
        "same-instance activations serialize while independent declared "
        "effects can run"
    );
    test_check(
        unsafe_effects->blocking_node_id != 0 && unsafe_effects->blocking_reason,
        "compile reports the node and effect that blocks concurrency"
    );
    flow_parallel_for_fn host_parallel = reverse_parallel_for;
    test_check(host_parallel != NULL, "parallel execution is a host-supplied callback, not a core thread pool");

    ecs_world *ecs = ecs_world_new();
    ecs_world_register_component(ecs, &GAME_COMPONENT_GameHealth, NULL);
    ecs_entity e1 = ecs_world_create_entity(ecs);
    ecs_entity e2 = ecs_world_create_entity(ecs);
    game_live_world *live = game_live_world_new(ecs);
    game_live_definition *definition = game_live_definition_new("parallel", make_live_behavior_asset());
    game_live_add_definition(live, definition);
    game_behavior_world *behaviors = live->behaviors;
    game_behavior_attach(behaviors, e1, "parallel");
    game_behavior_attach(behaviors, e2, "parallel");
    g_parallel_calls = 0;
    game_behavior_set_parallel_for(behaviors, host_parallel, NULL);
    game_behavior_pump(behaviors, 1.0);
    test_check(g_parallel_calls == 1, "host parallel dispatch reaches reflected behavior effects");
    game_behavior_set_breakpoints_enabled(behaviors, 1);
    g_parallel_calls = 0;
    game_behavior_pump(behaviors, 2.0);
    test_check(g_parallel_calls == 0, "enabling a breakpoint forces single-threaded behavior execution");
    game_live_world_free(live);
    ecs_world_free(ecs);

    ecs_world *first = ecs_world_new();
    ecs_world *second = ecs_world_new();
    ecs_world_register_component(first, &GAME_COMPONENT_GameHealth, NULL);
    ecs_world_register_component(second, &GAME_COMPONENT_GameHealth, NULL);
    ecs_entity first_entity = ecs_world_create_entity(first);
    ecs_entity second_entity = ecs_world_create_entity(second);
    GAME_GameHealth_add(first, first_entity, 1);
    GAME_GameHealth_add(second, second_entity, 1);
    ecs_world_apply_commands(first);
    ecs_world_apply_commands(second);
    GAME_GameHealth_get(first, first_entity)->current = 42;
    GAME_GameHealth_get(second, second_entity)->current = 42;
    test_check(ecs_world_hash(first) == ecs_world_hash(second), "a seeded world hash is identical across execution scheduling");
    ecs_world_free(first);
    ecs_world_free(second);
    compile_report_free(&safe_report);
    compile_report_free(&unsafe_report);
    behavior_asset_free(safe);
    behavior_asset_free(unsafe);
}

void run_scene_checks(void)
{
    game_scene *one = game_scene_new(64, 16, UINT64_C(0x12345678));
    game_scene *many = game_scene_new(64, 16, UINT64_C(0x12345678));
    game_scene_set_parallel_for(many, reverse_parallel_for, NULL);
    int deterministic = one && many;
    for (int frame = 0; deterministic && frame < 600; frame++) {
        deterministic &= game_scene_frame(one, (double)frame / 60.0) == ECS_OK;
        deterministic &= game_scene_frame(many, (double)frame / 60.0) == ECS_OK;
        deterministic &= game_scene_hash(one) == game_scene_hash(many);
    }
    test_check(deterministic, "a seeded 600-frame scene has identical per-frame hashes at one and many workers");
    game_scene_free(one);
    game_scene_free(many);

#ifndef CHIBI_SKIP_PERFORMANCE_TESTS
    game_scene *budget = game_scene_new(2000, 500, UINT64_C(0xdecafbad));
    double total_behavior_ms = 0.0;
    int budget_ok = budget != NULL;
    for (int frame = 0; budget_ok && frame < 20; frame++) {
        budget_ok &= game_scene_frame(budget, (double)frame / 60.0) == ECS_OK;
        total_behavior_ms += game_scene_behavior_ms(budget);
    }
    double average_behavior_ms = total_behavior_ms / 20.0;
    test_check(
        budget_ok && game_scene_entity_count(budget) == 2000 &&
            game_scene_behavior_count(budget) == 500 && average_behavior_ms < 2.0,
        "2000 live entities with 500 per-frame behaviors stay under 2 "
        "ms average behavior time"
    );
    game_scene_free(budget);
#endif
}
