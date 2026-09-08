#include "test/suite_internal.h"

static int g_engine_system_frames;
static void engine_test_system(ecs_world *world, double now, void *user)
{
    (void)world;
    (void)now;
    (void)user;
    g_engine_system_frames++;
}

void run_engine_checks(void)
{
    ecs_world *ecs = ecs_world_new();
    game_live_world *live = game_live_world_new(ecs);
    game_live_definition *definition = game_live_definition_new("engine", make_live_behavior_asset());
    ecs_entity entity = ecs_world_create_entity(ecs);
    test_check(
        live && definition && game_live_add_definition(live, definition) == 0 &&
            game_behavior_attach(live->behaviors, entity, "engine") == 0,
        "the world and behavior attachment run headlessly without a "
        "window"
    );
    engine *runtime = engine_new(ecs, live);
    g_engine_system_frames = 0;
    test_check(
        runtime && engine_add_system(runtime, engine_test_system, NULL) == 0 &&
            engine_set_focus(runtime, ENGINE_FOCUS_VIEWPORT) == 0,
        "the host exposes one explicit viewport focus"
    );
    flow_error error = { 0 };
    test_check(
        engine_dispatch_input(runtime, "Jump", 1.0, 1, &error) == 1 && error.code == 0,
        "generic input reaches attached behaviors through typed event dispatch"
    );
    test_check(
        engine_frame(runtime, 1.0) == FLOW_OK && engine_frame(runtime, 2.0) == FLOW_OK &&
            engine_route_escape(runtime) == 0 &&
            engine_get_focus(runtime) == ENGINE_FOCUS_NONE,
        "each frame advances monotonic behavior then applies commands "
        "once"
    );
    const engine_frame_timing *timing = engine_last_timing(runtime);
    test_check(
        timing && timing->frame == 2 && timing->command_apply_count == 2 &&
            g_engine_system_frames == 2 && timing->behavior_ms >= 0.0 &&
            timing->systems_ms >= 0.0 && timing->rendering_ms >= 0.0,
        "behavior, systems, and rendering timing are separately "
        "reportable"
    );
    test_check(
        runtime && engine_set_playing(runtime, false) == 0 &&
            !engine_is_playing(runtime) && engine_step(runtime) == 0 &&
            engine_frame(runtime, 3.0) == FLOW_OK && !engine_is_playing(runtime),
        "paused runtime advances only for an explicit step"
    );
    engine_free(runtime);
    game_live_world_free(live);
    ecs_world_free(ecs);
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

#ifndef CBLUEPRINT_SKIP_PERFORMANCE_TESTS
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
