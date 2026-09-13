#include "ecs/execution.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/update.h"
#include "runtime/rhythm.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_rhythm_fixture.h"

#include <dlfcn.h>
#include <float.h>
#include <time.h>

enum
{
    VALID,
    UNREGISTERED,
    EMPTY,
    DUPLICATE,
    MISSING_WORLD,
    NAN_TIME,
    INF_TIME,
    NEGATIVE_TIME,
    ZERO_BPM,
    NAN_BPM,
    INF_WIDTH,
    NEGATIVE_WIDTH,
    WIDE_PULSE,
    TINY_BPM,
    CHANGED_AT_WAIT,
    REMOVED_AT_WAIT,
    SCENARIOS
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
            "rhythm exclusion: %s graph=%s node=%llu pin=%d\n",
            d->reason,
            d->graph->name,
            (unsigned long long)d->node->id,
            d->pin
        );
    }
    assert(f && f->custom_lowering && f->component_access);
    for (int i = 0; i < acount(f->instructions); i++) {
        behavior_function_op op = f->instructions[i].op;
        assert(!behavior_function_call(op) || op == BF_FLOOR);
    }
    return f;
}
void emit_rhythm_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int waits = 0; waits < 2; waits++) {
        behavior_asset *a = whole_rhythm_fixture(waits);
        behavior_function_write_c(entry(a), out, waits ? "generated_rhythm_wait" : "generated_rhythm");
        behavior_asset_free(a);
    }
    assert(!fclose(out));
}
static ecs_world *new_world(int scenario, ecs_entity *clock)
{
    ecs_world *world = ecs_world_new();
    if (scenario == UNREGISTERED) {
        return world;
    }
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameRhythm, NULL));
    if (scenario == EMPTY) {
        return world;
    }
    *clock = ecs_world_create_entity(world);
    GameRhythm settings = { 60, .25f };
    if (scenario == ZERO_BPM) {
        settings.bpm = 0;
    }
    if (scenario == NAN_BPM) {
        settings.bpm = NAN;
    }
    if (scenario == INF_WIDTH) {
        settings.pulse_width = INFINITY;
    }
    if (scenario == NEGATIVE_WIDTH) {
        settings.pulse_width = -.1f;
    }
    if (scenario == WIDE_PULSE) {
        settings.pulse_width = 20;
    }
    if (scenario == TINY_BPM) {
        settings.bpm = FLT_TRUE_MIN;
    }
    assert(!ecs_world_record_add(world, *clock, GAME_COMPONENT_GameRhythm.id, &settings, 0));
    if (scenario == DUPLICATE) {
        ecs_entity second = ecs_world_create_entity(world);
        assert(!ecs_world_record_add(world, second, GAME_COMPONENT_GameRhythm.id, &settings, 0));
    }
    assert(!ecs_world_apply_commands(world));
    return world;
}
static void parity(bool waits, int scenario, int control, int budget, void *library)
{
    behavior_asset *a = whole_rhythm_fixture(waits);
    graph *apply = a->graphs[2];
    assert(!graph_set_breakpoint(apply, apply->nodes[1], control == 3));
    behavior_function *f = entry(a);
    void *code = f->code, *inspection = f->inspection_code, *dispatch = f->dispatch;
    test_flow_snapshot *expected = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            f->code = dlsym(library, waits ? "generated_rhythm_wait" : "generated_rhythm");
            assert(f->code);
            f->inspection_code = f->code;
            f->dispatch = NULL;
        }
        ecs_entity clock = { 0 };
        ecs_world *world = new_world(scenario, &clock);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(bi, control != 0);
        behavior_instance_set_breakpoints_enabled(bi, control == 3);
        if (scenario != MISSING_WORLD) {
            assert(!behavior_instance_bind_service(bi, "ecs.world", world));
        }
        bi->current_time = scenario == NAN_TIME      ? NAN
                         : scenario == INF_TIME      ? INFINITY
                         : scenario == NEGATIVE_TIME ? -.125
                                                     : .125;
        /* Deliberately different from the instance clock, including after resume. */
        flow_options options = { .instance = bi, .step_budget = budget, .initial_time = 10 };
        flow_run *run = flow_run_start(a->graph, a->graph->nodes[0], (flow_inputs){ 0 }, &options, NULL);
        assert(run);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = control == 1 ? flow_run_step(run) : flow_run_resume(run), at = 0;
        for (;;) {
            test_flow_snapshot snapshot = test_flow_snapshot_take(run, bi, result);
            if (!mode) {
                apush(expected, snapshot);
            } else {
                assert(run->whole_function_executed && at < acount(expected));
                test_flow_snapshot_same(snapshot, expected[at]);
                test_flow_snapshot_free(&snapshot);
            }
            assert(++at < 64);
            flow_state status = flow_run_status(run);
            if (status == FLOW_COMPLETED || status == FLOW_FAILED) {
                if (scenario == VALID && budget == 100) {
                    assert(
                        status == FLOW_COMPLETED &&
                        bi->variables[0].d == .125 && bi->variables[1].d == .25
                    );
                }
                break;
            }
            if (status == FLOW_SUSPENDED) {
                if (control == 2) {
                    assert(!flow_run_stop(run));
                    break;
                }
                if (scenario == CHANGED_AT_WAIT) {
                    *GAME_GameRhythm_get(world, clock) = (GameRhythm){ 120, .5f };
                    bi->current_time = .375;
                }
                if (scenario == REMOVED_AT_WAIT) {
                    assert(!ecs_world_destroy_entity(world, clock));
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
    f->code = code;
    f->inspection_code = inspection;
    f->dispatch = dispatch;
    behavior_asset_free(a);
}
static void prepared_clocks()
{
    behavior_asset *a = whole_rhythm_fixture(false);
    const behavior_function *f = entry(a);
    ecs_entity clock = { 0 };
    ecs_world *world = new_world(VALID, &clock);
    behavior_update_binding **bindings = NULL;
    behavior_update_row rows[2];
    for (int i = 0; i < 2; i++) {
        ecs_entity entity = ecs_world_create_entity(world);
        pin_value self = { .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .u64 = ecs_entity_to_u64(entity) };
        behavior_instance *bi = behavior_instance_create(a, &self, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        assert(!behavior_instance_bind_service(bi, "ecs.world", world));
        bi->current_time = .125 + .5 * i;
        behavior_update_binding
            *binding = behavior_update_prepare(&bindings, bi, f, a->graph->nodes[0]);
        assert(binding);
        rows[i] = (behavior_update_row){ .instance = bi,
                                         .binding = binding,
                                         .services_revision = bi->services_revision,
                                         .entity = self.u64 };
    }
    assert(acount(bindings) == 1);
    behavior_update_refresh(bindings);
    void *code = behavior_update_compile();
    assert(code);
    int result = 0;
    assert(
        ((behavior_update_fn)code)(rows, rows + 2, (const ecs_execution_view *)world, &result) ==
            rows + 2 &&
        !result
    );
    assert(
        rows[0].instance->variables[0].d == .125 &&
        rows[1].instance->variables[0].d == .625
    );
    for (int i = 0; i < 2; i++) {
        behavior_instance_destroy(rows[i].instance);
    }
    behavior_update_free_code(code);
    behavior_update_free_bindings(bindings);
    behavior_asset_free(a);
    ecs_world_free(world);
}
void test_compiled_rhythm(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    for (int waits = 0; waits < 2; waits++) {
        for (int scenario = 0; scenario < SCENARIOS; scenario++) {
            parity(waits, scenario, 0, 100, library);
            parity(waits, scenario, 1, 100, library);
        }
        for (int budget = 1; budget < 8; budget++) {
            parity(waits, VALID, 0, budget, library);
        }
        parity(waits, VALID, 2, 100, library);
        parity(waits, VALID, 3, 100, library);
    }
    prepared_clocks();
    whole_rhythm_smoke();
    if (library) {
        assert(!dlclose(library));
    }
    puts("PASS: whole rhythm defaults, singleton changes, invalid clocks, nested source/inspection, waits, cancellation and prepared instance clocks");
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_rhythm(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    behavior_asset *a = whole_rhythm_fixture(false);
    behavior_function *f = entry(a);
    int (*generated)(
        behavior_function_context *
    ) = (int (*)(behavior_function_context *))dlsym(library, "generated_rhythm");
    int (*native)(
        behavior_function_context *
    ) = (int (*)(behavior_function_context *))behavior_function_entry(f);
    assert(generated && native);
    for (int bound = 0; bound < 2; bound++) {
        for (int order = 0; order < 3; order++) {
            int mode = (order + rotation) % 3;
            ecs_entity clock = { 0 };
            ecs_world *world = new_world(VALID, &clock);
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            bi->current_time = .125;
            const ecs_execution_view *view = (const ecs_execution_view *)world;
            const ecs_execution_column *column = ecs_execution_find_column(
                view,
                GAME_COMPONENT_GameRhythm.id
            );
            behavior_function_context ctx = { .world = view,
                                              .budget = 100,
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
            assert(
                bi->variables[0].d == .125 && bi->variables[1].d == .25 &&
                bi->variables[2].d == 1 && bi->variables[3].d == .25
            );
            printf(
                "%s rhythm_%s ns=%.3f checksum=%.3f\n",
                (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C" }[mode],
                bound ? "bound" : "lookup",
                elapsed * 1e9 / count,
                bi->variables[1].d
            );
            behavior_function_activation_free(&activation);
            behavior_instance_destroy(bi);
            ecs_world_free(world);
        }
    }
    behavior_asset_free(a);
    assert(!dlclose(library));
    return 0;
}
