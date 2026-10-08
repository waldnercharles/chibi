#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_particle_fixture.h"

#include <dlfcn.h>
#include <time.h>

static behavior_function *entry(behavior_asset *a)
{
    behavior_program *p = behavior_program_publish(a);
    const behavior_graph_program *g = behavior_program_graph(p, a->graph);
    behavior_function *f = (behavior_function *)
        behavior_program_function(g, a->graph->nodes[0]);
    if (!f) {
        const behavior_function_diagnostic
            *d = behavior_program_function_diagnostic(g, a->graph->nodes[0]);
        fprintf(
            stderr,
            "particle exclusion: %s node=%llu pin=%d\n",
            d->reason,
            (unsigned long long)d->node->id,
            d->pin
        );
    }
    assert(f);
    return f;
}
void emit_particle_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int directed = 0; directed < 2; directed++) {
        for (int waits = 0; waits < 2; waits++) {
            for (int random = 0; random < 2; random++) {
                behavior_asset *a = whole_particle_fixture(directed, waits, random);
                char name[64];
                snprintf(name, sizeof name, "generated_particles_%d_%d_%d", directed, waits, random);
                behavior_function_write_c(entry(a), out, name);
                behavior_asset_free(a);
            }
        }
    }
    assert(!fclose(out));
}
static pin_value *arguments(behavior_asset *a, float vectors[2][2])
{
    node *event = a->graph->nodes[0];
    pin_value *args = NULL;
    int vector = 0;
    for (int i = 1; i < event->output_count; i++) {
        pin_value value = event->output_pins[i].default_value;
        if (value.type == PIN_STRUCT) {
            memcpy(vectors[vector], value.ptr, sizeof vectors[0]);
            value.ptr = vectors[vector++];
        }
        apush(args, value);
    }
    return args;
}
static void parity(bool directed, bool waits, int scenario, int control, int budget, void *library)
{
    behavior_asset *a = whole_particle_fixture(directed, waits, true);
    graph *g = a->graphs[1];
    assert(!graph_set_breakpoint(g, g->nodes[3], control == 3));
    behavior_function *f = entry(a);
    void *code = f->artifact->code, *inspection = f->artifact->inspection_code,
         *dispatch = f->artifact->dispatch;
    test_flow_snapshot *expected = NULL;
    game_particle **states = NULL;
    int next = 0;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            char name[64];
            snprintf(name, sizeof name, "generated_particles_%d_%d_1", directed, waits);
            f->artifact->code = dlsym(library, name);
            assert(f->artifact->code);
            f->artifact->inspection_code = f->artifact->code;
            f->artifact->dispatch = NULL;
        }
        game_particles *particles = game_particles_new();
        if (scenario == 8) {
            game_particle_burst fill = { .count = GAME_PARTICLE_LIMIT - 1,
                                         .lifetime = 1 };
            assert(game_particles_emit(particles, &fill) == GAME_PARTICLE_LIMIT - 1);
        }
        float vectors[2][2];
        pin_value *args = arguments(a, vectors);
        if (scenario == 2) {
            args[0].ptr = NULL;
        }
        if (scenario == 3) {
            vectors[0][1] = NAN;
        }
        if (scenario == 4) {
            args[2].i = -1;
        }
        if (scenario == 5) {
            args[directed ? 7 : 4].f = 0;
        }
        if (scenario == 6) {
            if (directed) {
                args[1].ptr = NULL;
            } else {
                vectors[0][0] = INFINITY;
            }
        }
        if (scenario == 7) {
            if (directed) {
                args[18].i = GAME_PARTICLE_SHAPE_COUNT;
            } else {
                args[4].f = INFINITY;
            }
        }
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(bi, control != 0);
        behavior_instance_set_breakpoints_enabled(bi, control == 3);
        if (scenario != 1) {
            assert(!behavior_instance_bind_service(bi, "game.particles", particles));
        }
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_run *run = flow_run_start(
            a->graph,
            a->graph->nodes[0],
            (flow_inputs){ args, acount(args) },
            &options,
            NULL
        );
        assert(run);
        memset(vectors, 0, sizeof vectors);
        afree(args);
        srand(91);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = control == 1 ? flow_run_step(run) : flow_run_resume(run), at = 0;
        for (;;) {
            test_flow_snapshot snapshot = test_flow_snapshot_take(run, bi, result);
            int count = game_particles_count(particles);
            const game_particle *data = game_particles_data(particles);
            if (!mode) {
                apush(expected, snapshot);
                game_particle *state = NULL;
                asetlen(state, count);
                if (count) {
                    memcpy(state, data, (size_t)count * sizeof *state);
                }
                apush(states, state);
            } else {
                assert(run->whole_function_executed && at < acount(expected));
                test_flow_snapshot_same(snapshot, expected[at]);
                test_flow_snapshot_free(&snapshot);
                assert(count == acount(states[at]));
                assert(!count || !memcmp(data, states[at], (size_t)count * sizeof *data));
            }
            assert(++at < 64);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                if (!scenario && budget == 100) {
                    assert(state == FLOW_COMPLETED && count > 0);
                }
                break;
            }
            if (control == 2 && state == FLOW_SUSPENDED) {
                assert(!flow_run_stop(run));
                break;
            }
            result = state == FLOW_SUSPENDED
                       ? flow_run_tick_at(run, flow_run_wake_time(run))
                       : flow_run_step(run);
        }
        if (!mode) {
            next = rand();
        } else {
            assert(at == acount(expected) && rand() == next);
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        }
        flow_run_free(run);
        behavior_instance_destroy(bi);
        game_particles_free(particles);
    }
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
        afree(states[i]);
    }
    afree(states);
    afree(expected);
    f->artifact->code = code;
    f->artifact->inspection_code = inspection;
    f->artifact->dispatch = dispatch;
    behavior_asset_free(a);
}
void test_compiled_particles(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    for (int directed = 0; directed < 2; directed++) {
        for (int waits = 0; waits < 2; waits++) {
            for (int scenario = 0; scenario < 9; scenario++) {
                parity(directed, waits, scenario, 0, 100, library);
                parity(directed, waits, scenario, 1, 100, library);
            }
            for (int budget = 1; budget <= 3; budget++) {
                parity(directed, waits, 0, 0, budget, library);
            }
            parity(directed, waits, 0, 2, 100, library);
            parity(directed, waits, 0, 3, 100, library);
        }
    }
    if (library) {
        assert(!dlclose(library));
    }
    puts("PASS: whole particle state, errors, service guards, draw order, nested waits, cancellation and inspection");
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_particles(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int directed = 0; directed < 2; directed++) {
        behavior_asset *a = whole_particle_fixture(directed, false, false);
        behavior_function *f = entry(a);
        char name[64];
        snprintf(name, sizeof name, "generated_particles_%d_0_0", directed);
        int (*generated)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))dlsym(library, name);
        assert(generated);
        int (*native)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))behavior_function_entry(f);
        const node_host_call *host = a->graphs[1]->nodes[2]->desc->host_call;
        float vectors[2][2];
        pin_value *args = arguments(a, vectors);
        for (int size = 0; size < 3; size++) {
            int particles_per_call = (int[]){ 0, 1, 32 }[size];
            args[2].i = particles_per_call;
            uint64_t expected = 0;
            for (int order = 0; order < 4; order++) {
                int mode = (order + rotation) % 4;
                game_particles *particles = game_particles_new();
                behavior_function_context ctx = { .arguments = args,
                                                  .argument_count = acount(args),
                                                  .budget = 100,
                                                  .services = NULL };
                service_binding *bindings = NULL;
                apush(bindings, ((service_binding){ "game.particles", particles }));
                ctx.services = bindings;
                behavior_function_activation activation = { 0 };
                behavior_function_activation_init(f, &activation);
                ctx.activation = &activation;
                behavior_function_prepare_host(f, &ctx);
                uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(),
                         data = 0, vm = 0;
                double start = 0;
                for (int i = -64; i < count; i++) {
                    if (!i) {
                        data = behavior_function_data_allocations();
                        vm = behavior_function_vm_allocations();
                        start = seconds();
                    }
                    game_particles_clear(particles);
                    ctx.steps = 0;
                    if (mode == 3) {
                        assert(!host->invoke(particles, args, NULL));
                        assert(!host->invoke(particles, args, NULL));
                    } else {
                        int status = mode == 2 ? generated(&ctx)
                                   : mode == 0
                                       ? native(&ctx)
                                       : behavior_function_run(f, &ctx, false);
                        assert(!status);
                    }
                }
                double elapsed = seconds() - start;
                assert(
                    data == behavior_function_data_allocations() &&
                    vm == behavior_function_vm_allocations()
                );
                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
                int emitted = game_particles_count(particles);
                assert(emitted == particles_per_call * 2);
                uint64_t checksum = 0;
                const unsigned char *bytes = (const unsigned char *)
                    game_particles_data(particles);
                for (size_t i = 0; i < (size_t)emitted * sizeof(game_particle); i++) {
                    checksum = checksum * 31 + bytes[i];
                }
                if (!order) {
                    expected = checksum;
                }
                assert(checksum == expected);
                printf(
                    "%s particles_%s_%d ns=%.3f checksum=%llu\n",
                    (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C", "C_HOST" }[mode],
                    directed ? "spray" : "burst",
                    particles_per_call,
                    elapsed * 1e9 / count,
                    (unsigned long long)checksum
                );
                behavior_function_release_host(&ctx);
                behavior_function_activation_free(&activation);
                afree(bindings);
                game_particles_free(particles);
            }
        }
        afree(args);
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    return 0;
}
