#include "ecs/execution.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "runtime/visual_motion.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_reaction_fixture.h"

#include <dlfcn.h>
#include <float.h>
#include <time.h>

enum
{
    VALID,
    NULL_ROTATION,
    INVALID_LIFT,
    NAN_ROTATION_X,
    INFINITE_ROTATION_Y,
    INFINITE_ROTATION_Z,
    NAN_DURATION,
    NEGATIVE_DURATION,
    MISSING_WORLD,
    MISSING_COMPONENT,
    ZERO_TARGET,
    STALE_TARGET,
    ZERO_DURATION,
    EXTREME_VALUES,
    INVALID_LIFT_WITHOUT_WORLD,
    INVALID_ROTATION_WITHOUT_WORLD,
    CHANGED_DURATION_AT_WAIT,
    REMOVED_COMPONENT_AT_WAIT,
    INFINITE_DURATION,
    SCENARIO_COUNT
};
static behavior_function *entry(behavior_asset *a)
{
    const behavior_graph_program
        *p = behavior_program_graph(behavior_program_publish(a), a->graph);
    behavior_function *f = (behavior_function *)
        behavior_program_function(p, a->graph->nodes[0]);
    if (!f) {
        const behavior_function_diagnostic
            *d = behavior_program_function_diagnostic(p, a->graph->nodes[0]);
        fprintf(
            stderr,
            "reaction exclusion: %s graph=%s node=%llu pin=%d\n",
            d->reason,
            d->graph->name,
            (unsigned long long)d->node->id,
            d->pin
        );
    }
    assert(f && f->custom_lowering && f->component_access);
    assert(
        acount(f->component_writes) == 1 &&
        f->component_writes[0] == GAME_COMPONENT_GameVisualMotion.id
    );
    for (int i = 0; i < acount(f->instructions); i++) {
        assert(!behavior_function_call(f->instructions[i].op));
    }
    return f;
}
void emit_reaction_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int waits = 0; waits < 2; waits++) {
        for (int target = 0; target < 2; target++) {
            behavior_asset *a = whole_reaction_fixture(waits, target);
            char name[64];
            snprintf(name, sizeof name, "generated_reaction_%d_%d", waits, target);
            behavior_function_write_c(entry(a), out, name);
            behavior_asset_free(a);
        }
    }
    assert(!fclose(out));
}
static ecs_world *world_new()
{
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameVisualMotion, NULL));
    return world;
}
static GameVisualMotion state(ecs_world *world, ecs_entity entity)
{
    const GameVisualMotion *value = GAME_GameVisualMotion_get_const(world, entity);
    return value ? *value : (GameVisualMotion){ 0 };
}
static void parity(bool waits, bool target, int scenario, int control, int budget, void *library)
{
    behavior_asset *a = whole_reaction_fixture(waits, target);
    graph *g = a->graphs[1];
    assert(!graph_set_breakpoint(g, g->nodes[2], control == 3));
    behavior_function *f = entry(a);
    void *code = f->code, *inspection = f->inspection_code, *dispatch = f->dispatch;
    test_flow_snapshot *expected = NULL;
    GameVisualMotion *states = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            char name[64];
            snprintf(name, sizeof name, "generated_reaction_%d_%d", waits, target);
            f->code = dlsym(library, name);
            assert(f->code);
            f->inspection_code = f->code;
            f->dispatch = NULL;
        }
        ecs_world *world = world_new();
        ecs_entity owner = ecs_world_create_entity(world),
                   other = ecs_world_create_entity(world);
        assert(!GAME_GameVisualMotion_add(world, owner, 0));
        assert(!GAME_GameVisualMotion_add(world, other, 0));
        assert(!ecs_world_apply_commands(world));
        ecs_entity selected = target ? other : owner;
        GameVisualMotion initial = { .rotation = { 1, 2, 3 },
                                     .angular_velocity = { 4, 5, 6 },
                                     .speed = 7,
                                     .rest_elevation = 8,
                                     .reaction_rotation = { 9, 10, 11 },
                                     .reaction_lift = 12,
                                     .reaction_age = 13,
                                     .reaction_duration = .25f };
        *GAME_GameVisualMotion_get(world, owner) = initial;
        *GAME_GameVisualMotion_get(world, other) = initial;
        GameVisualMotion *motion = GAME_GameVisualMotion_get(world, selected);
        if (scenario == NAN_DURATION) {
            motion->reaction_duration = NAN;
        }
        if (scenario == NEGATIVE_DURATION) {
            motion->reaction_duration = -.1f;
        }
        if (scenario == ZERO_DURATION) {
            motion->reaction_duration = 0;
        }
        if (scenario == EXTREME_VALUES) {
            motion->reaction_duration = -0.0f;
        }
        if (scenario == INFINITE_DURATION) {
            motion->reaction_duration = INFINITY;
        }
        if (scenario == MISSING_COMPONENT) {
            assert(!GAME_GameVisualMotion_remove(world, selected, 0));
            assert(!ecs_world_apply_commands(world));
        }
        pin_value self = { .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .u64 = ecs_entity_to_u64(owner) };
        behavior_instance *bi = behavior_instance_create(a, &self, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(bi, control != 0);
        behavior_instance_set_breakpoints_enabled(bi, control == 3);
        if (scenario != MISSING_WORLD && scenario != INVALID_LIFT_WITHOUT_WORLD &&
            scenario != INVALID_ROTATION_WITHOUT_WORLD) {
            assert(!behavior_instance_bind_service(bi, "ecs.world", world));
        }
        CF_V3 rotation = { -.25f, .5f, .75f };
        if (scenario == NAN_ROTATION_X || scenario == INVALID_ROTATION_WITHOUT_WORLD) {
            rotation.x = NAN;
        }
        if (scenario == INFINITE_ROTATION_Y) {
            rotation.y = INFINITY;
        }
        if (scenario == INFINITE_ROTATION_Z) {
            rotation.z = -INFINITY;
        }
        if (scenario == EXTREME_VALUES) {
            rotation = (CF_V3){ -0.0f, FLT_MAX, -FLT_MAX };
        }
        pin_value args[] = {
            { .type = PIN_STRUCT,
              .type_name = "CF_V3",
              .ptr = scenario == NULL_ROTATION ? NULL : &rotation },
            { .type = PIN_F32,
              .f = scenario == INVALID_LIFT || scenario == INVALID_LIFT_WITHOUT_WORLD
                     ? NAN
                     : 2.5f },
            { .type = PIN_HANDLE,
              .type_name = "ecs.entity",
              .u64 = scenario == ZERO_TARGET ? 0 : ecs_entity_to_u64(other) }
        };
        if (scenario == STALE_TARGET) {
            args[2].u64 ^= UINT64_C(1) << 32;
        }
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_run *run = flow_run_start(a->graph, a->graph->nodes[0], (flow_inputs){ args, 3 }, &options, NULL);
        assert(run);
        memset(&rotation, 0, sizeof rotation);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = control == 1 ? flow_run_step(run) : flow_run_resume(run), at = 0;
        for (;;) {
            test_flow_snapshot snapshot = test_flow_snapshot_take(run, bi, result);
            GameVisualMotion observed[] = { state(world, owner), state(world, other) };
            if (!mode) {
                apush(expected, snapshot);
                apush(states, observed[0]);
                apush(states, observed[1]);
            } else {
                assert(run->whole_function_executed && at < acount(expected));
                test_flow_snapshot_same(snapshot, expected[at]);
                test_flow_snapshot_free(&snapshot);
                assert(!memcmp(observed, &states[at * 2], sizeof observed));
            }
            assert(++at < 64);
            flow_state status = flow_run_status(run);
            if (status == FLOW_COMPLETED || status == FLOW_FAILED) {
                if (budget == 100 && (scenario == VALID || scenario == ZERO_DURATION ||
                                      scenario == EXTREME_VALUES)) {
                    assert(status == FLOW_COMPLETED && bi->variables[0].b);
                    assert(
                        observed[target].reaction_age == 0 &&
                        observed[target].reaction_lift == 2.5f
                    );
                    assert(
                        observed[target].reaction_rotation.x ==
                        (scenario == EXTREME_VALUES ? -0.0f : -.25f)
                    );
                    assert(!memcmp(&observed[!target], &initial, sizeof initial));
                }
                break;
            }
            if (status == FLOW_SUSPENDED) {
                if (control == 2) {
                    assert(!flow_run_stop(run));
                    break;
                }
                if (scenario == CHANGED_DURATION_AT_WAIT) {
                    GAME_GameVisualMotion_get(world, selected)->reaction_duration = -1;
                }
                if (scenario == REMOVED_COMPONENT_AT_WAIT) {
                    assert(!GAME_GameVisualMotion_remove(world, selected, 0));
                    assert(!ecs_world_apply_commands(world));
                }
            }
            result = status == FLOW_SUSPENDED
                       ? flow_run_tick_at(run, flow_run_wake_time(run))
                       : flow_run_step(run);
        }
        if (mode) {
            assert(at == acount(expected));
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        }
        flow_run_free(run);
        behavior_instance_destroy(bi);
        ecs_world_free(world);
    }
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
    }
    afree(expected);
    afree(states);
    f->code = code;
    f->inspection_code = inspection;
    f->dispatch = dispatch;
    behavior_asset_free(a);
}
void test_compiled_reaction(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    for (int waits = 0; waits < 2; waits++) {
        for (int target = 0; target < 2; target++) {
            for (int scenario = 0; scenario < SCENARIO_COUNT; scenario++) {
                parity(waits, target, scenario, 0, 100, library);
                parity(waits, target, scenario, 1, 100, library);
            }
            for (int budget = 1; budget <= 5; budget++) {
                parity(waits, target, 0, 0, budget, library);
            }
            parity(waits, target, 0, 2, 100, library);
            parity(waits, target, 0, 3, 100, library);
        }
    }
    behavior_asset *a = behavior_asset_new("Reaction effects");
    node *reaction = graph_add_node(a->graph, &NODE_Set_Visual_Reaction);
    compile_report report;
    compile_report_init(&report);
    assert(!behavior_asset_compile(a, &report));
    const compile_effects *effects = compile_report_effects(&report);
    assert(effects && !effects->parallel_eligible && !effects->all_writes);
    assert(effects->blocking_node_id == reaction->id);
    assert(
        acount(effects->writes) == 1 &&
        effects->writes[0] == GAME_COMPONENT_GameVisualMotion.id
    );
    compile_report_free(&report);
    behavior_asset_free(a);
    if (library) {
        assert(!dlclose(library));
    }
    whole_reaction_smoke();
    puts("PASS: whole visual reaction, source/error priority, atomic writes, target generations, waits, cancellation, inspection and effect safety");
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_reaction(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int bound = 0; bound < 2; bound++) {
        behavior_asset *a = whole_reaction_fixture(false, true);
        behavior_function *f = entry(a);
        int (*generated)(behavior_function_context *) = (int (*)(
            behavior_function_context *
        ))dlsym(library, "generated_reaction_0_1");
        int (*native)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))behavior_function_entry(f);
        assert(generated && native);
        for (int order = 0; order < 3; order++) {
            int mode = (order + rotation) % 3;
            ecs_world *world = world_new();
            ecs_entity entity = ecs_world_create_entity(world);
            assert(!GAME_GameVisualMotion_add(world, entity, 0));
            assert(!ecs_world_apply_commands(world));
            GAME_GameVisualMotion_get(world, entity)->reaction_duration = .25f;
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            CF_V3 rotation = { .25f, -.5f, .75f };
            pin_value args[] = { { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &rotation },
                                 { .type = PIN_F32, .f = 2.5f },
                                 { .type = PIN_HANDLE,
                                   .type_name = "ecs.entity",
                                   .u64 = ecs_entity_to_u64(entity) } };
            const ecs_execution_view *view = (const ecs_execution_view *)world;
            const ecs_execution_column *column = ecs_execution_find_column(
                view,
                GAME_COMPONENT_GameVisualMotion.id
            );
            behavior_function_context ctx = { .arguments = args,
                                              .argument_count = 3,
                                              .budget = 100,
                                              .world = view,
                                              .columns = bound ? &column : NULL };
            behavior_instance_bind_values(bi, &ctx);
            behavior_function_activation activation = { 0 };
            behavior_function_activation_init(f, &activation);
            ctx.activation = &activation;
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(), vm = 0;
            double start = 0;
            for (int i = -10000; i < count; i++) {
                if (!i) {
                    vm = behavior_function_vm_allocations();
                    start = seconds();
                }
                ctx.steps = 0;
                int status = mode == 2 ? generated(&ctx)
                           : mode == 0 ? native(&ctx)
                                       : behavior_function_run(f, &ctx, false);
                assert(!status);
            }
            double elapsed = seconds() - start;
            assert(
                eval == eval_oracle_calls() && flow == flow_oracle_calls() &&
                vm == behavior_function_vm_allocations()
            );
            GameVisualMotion actual = state(world, entity);
            assert(
                actual.reaction_rotation.x == .25f &&
                actual.reaction_rotation.y == -.5f && actual.reaction_rotation.z == .75f
            );
            assert(
                actual.reaction_age == 0 && actual.reaction_lift == 2.5f &&
                bi->variables[0].b
            );
            printf(
                "%s reaction_%s ns=%.3f checksum=%.3f\n",
                (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C" }[mode],
                bound ? "bound" : "lookup",
                elapsed * 1e9 / count,
                actual.reaction_lift
            );
            behavior_function_activation_free(&activation);
            behavior_instance_destroy(bi);
            ecs_world_free(world);
        }
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    return 0;
}
