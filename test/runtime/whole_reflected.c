#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_reflected_fixture.h"

#include <dlfcn.h>
#include <time.h>

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
            "reflected exclusion: %s graph=%s node=%llu pin=%d\n",
            d->reason,
            d->graph->name,
            (unsigned long long)d->node->id,
            d->pin
        );
    }
    assert(f && acount(f->host_calls) == 1);
    return f;
}
void emit_reflected_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < 2; kind++) {
        for (int waits = 0; waits < 2; waits++) {
            behavior_asset *a = whole_reflected_fixture(kind, waits);
            char name[64];
            snprintf(name, sizeof name, "generated_reflected_%d_%d", kind, waits);
            behavior_function_write_c(entry(a), out, name);
            behavior_asset_free(a);
        }
    }
    assert(!fclose(out));
}
static int arguments(
    int kind,
    int scenario,
    pin_value *args,
    GameGeometry3D *geometry,
    CF_V2 *offset,
    GameVisualTransform *visual
)
{
    *geometry = (GameGeometry3D){ .primitive = GAME_PRIMITIVE_BOX_FRAME,
                                  .width = 2,
                                  .height = 3,
                                  .depth = 4 };
    *offset = (CF_V2){ .5f, -2 };
    *visual = (GameVisualTransform){ .pitch = .25f, .yaw = -.5f, .roll = 1, .scale = 2 };
    if (kind == 0) {
        args[0] = (pin_value){ .type = PIN_STRUCT,
                               .type_name = "GameGeometry3D",
                               .ptr = scenario == 1 ? NULL : geometry };
        args[1] = (pin_value){ .type = PIN_I32,
                               .i = scenario == 2 ? 12
                                  : scenario == 3 ? -1
                                                  : 7 };
        if (scenario == 4) {
            geometry->width = NAN;
        }
        if (scenario == 5) {
            geometry->primitive = GAME_PRIMITIVE_BOX;
            args[1].i = 0;
        }
        if (scenario == 6) {
            geometry->primitive = -1;
        }
        return 2;
    }
    if (kind == 1) {
        args[0] = (pin_value){ .type = PIN_STRUCT,
                               .type_name = "CF_V2",
                               .ptr = scenario == 1 ? NULL : offset };
        args[1] = (pin_value){ .type = PIN_F32, .f = .25f };
        args[2] = (pin_value){ .type = PIN_F32, .f = .5f };
        args[3] = (pin_value){ .type = PIN_STRUCT,
                               .type_name = "GameVisualTransform",
                               .ptr = scenario == 2 ? NULL : visual };
        if (scenario == 3) {
            visual->scale = -1;
        }
        if (scenario == 4) {
            visual->pitch = NAN;
        }
        if (scenario == 5) {
            visual->scale = 0;
        }
        if (scenario == 6) {
            offset->x = INFINITY;
        }
        return 4;
    }
    assert(false);
    return 0;
}
static void parity(int kind, bool waits, int scenario, int control, int budget, void *library)
{
    behavior_asset *a = whole_reflected_fixture(kind, waits);
    graph *g = a->graphs[1];
    assert(!graph_set_breakpoint(g, g->nodes[1], control == 3));
    behavior_function *f = entry(a);
    void *code = f->code, *inspection = f->inspection_code, *dispatch = f->dispatch;
    test_flow_snapshot *expected = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            char name[64];
            snprintf(name, sizeof name, "generated_reflected_%d_%d", kind, waits);
            f->code = dlsym(library, name);
            assert(f->code);
            f->inspection_code = f->code;
            f->dispatch = NULL;
        }
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        /* A no-service helper must also work with unrelated bound services. */
        assert(!behavior_instance_bind_service(bi, "unrelated", bi));
        behavior_instance_set_trace_enabled(bi, control != 0);
        behavior_instance_set_breakpoints_enabled(bi, control == 3);
        GameGeometry3D geometry;
        CF_V2 offset;
        GameVisualTransform visual;
        pin_value args[4];
        int count = arguments(kind, scenario, args, &geometry, &offset, &visual);
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_run *run = flow_run_start(
            a->graph,
            a->graph->nodes[0],
            (flow_inputs){ args, count },
            &options,
            NULL
        );
        assert(run);
        memset(&geometry, 0, sizeof geometry);
        memset(&offset, 0, sizeof offset);
        memset(&visual, 0, sizeof visual);
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
            assert(++at < 80);
            flow_state status = flow_run_status(run);
            if (status == FLOW_COMPLETED || status == FLOW_FAILED) {
                if (!scenario && budget == 100) {
                    assert(status == FLOW_COMPLETED);
                }
                break;
            }
            if (status == FLOW_SUSPENDED && control == 2) {
                assert(!flow_run_stop(run));
                break;
            }
            result = status == FLOW_SUSPENDED
                       ? flow_run_tick_at(run, flow_run_wake_time(run))
                       : flow_run_step(run);
        }
        if (mode) {
            assert(
                at == acount(expected) && eval == eval_oracle_calls() &&
                flow == flow_oracle_calls()
            );
        }
        flow_run_free(run);
        behavior_instance_destroy(bi);
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
void test_compiled_reflected(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    for (int kind = 0; kind < 2; kind++) {
        for (int waits = 0; waits < 2; waits++) {
            for (int scenario = 0; scenario < 7; scenario++) {
                parity(kind, waits, scenario, 0, 100, library);
                parity(kind, waits, scenario, 1, 100, library);
            }
            for (int budget = 1; budget < 8; budget++) {
                parity(kind, waits, 0, 0, budget, library);
            }
            parity(kind, waits, 0, 2, 100, library);
            parity(kind, waits, 0, 3, 100, library);
        }
    }
    whole_reflected_smoke();
    if (library) {
        assert(!dlclose(library));
    }
    puts("PASS: reflected native calls, interleaved output parameters, projected fields, null inputs, errors, waits and inspection parity");
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_reflected(const char *path, int count, int rotation)
{
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library && count > 0 && rotation >= 0);
    for (int kind = 0; kind < 2; kind++) {
        behavior_asset *a = whole_reflected_fixture(kind, false);
        behavior_function *f = entry(a);
        char name[64];
        snprintf(name, sizeof name, "generated_reflected_%d_0", kind);
        int (*generated)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))dlsym(library, name);
        int (*native)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))behavior_function_entry(f);
        assert(native && generated);
        for (int order = 0; order < 3; order++) {
            int mode = (order + rotation) % 3;
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            GameGeometry3D geometry;
            CF_V2 offset;
            GameVisualTransform visual;
            pin_value args[4];
            int argc = arguments(kind, 0, args, &geometry, &offset, &visual);
            behavior_function_context ctx = { .arguments = args,
                                              .argument_count = argc,
                                              .budget = 100 };
            behavior_instance_bind_values(bi, &ctx);
            behavior_function_prepare_host(f, &ctx);
            behavior_function_activation activation = { 0 };
            behavior_function_activation_init(f, &activation);
            ctx.activation = &activation;
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(),
                     vm = 0, data = 0;
            double start = 0;
            for (int i = -10000; i < count; i++) {
                if (!i) {
                    vm = behavior_function_vm_allocations();
                    data = behavior_function_data_allocations();
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
                vm == behavior_function_vm_allocations() &&
                data == behavior_function_data_allocations()
            );
            double checksum = kind == 0 ? bi->variables[0].i : bi->variables[1].f;
            assert(isfinite(checksum));
            printf(
                "%s reflected_%d ns=%.3f checksum=%.6f\n",
                (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C" }[mode],
                kind,
                elapsed * 1e9 / count,
                checksum
            );
            behavior_function_activation_free(&activation);
            behavior_function_release_host(&ctx);
            behavior_instance_destroy(bi);
        }
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    return 0;
}
