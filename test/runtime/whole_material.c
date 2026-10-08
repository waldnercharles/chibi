#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_material_fixture.h"

#include <dlfcn.h>

static behavior_function *entry(behavior_asset *a)
{
    behavior_program *p = behavior_program_publish(a);
    const behavior_graph_program *g = behavior_program_graph(p, a->graph);
    behavior_function *f = (behavior_function *)
        behavior_program_function(g, a->graph->nodes[0]);
    if (!f) {
        const behavior_function_diagnostic
            *d = behavior_program_function_diagnostic(g, a->graph->nodes[0]);
        fprintf(stderr, "material exclusion: %s pin=%d\n", d->reason, d->pin);
    }
    assert(f);
    return f;
}
void emit_material_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < WHOLE_MATERIAL_KINDS; kind++) {
        for (int waits = 0; waits < 2; waits++) {
            behavior_asset *a = whole_material_fixture(kind, waits, true);
            char symbol[64];
            snprintf(symbol, sizeof symbol, "generated_material_%d_%d", kind, waits);
            behavior_function_write_c(entry(a), out, symbol);
            behavior_asset_free(a);
        }
    }
    emit_material_bench_c(out);
    assert(!fclose(out));
}
static void parity(int kind, bool waits, int scenario, unsigned budget, bool step, void *library)
{
    behavior_asset *a = whole_material_fixture(kind, waits, true);
    behavior_function *f = entry(a);
    void *code = f->artifact->code, *inspection = f->artifact->inspection_code,
         *dispatch = f->artifact->dispatch;
    int width = kind >= 3 && kind <= 5 ? kind - 1 : kind == 6 ? 2 : 1;
    int material = whole_material_publish(scenario == 7 ? (width == 1 ? 3 : 1) : width);
    test_flow_snapshot *expected = NULL;
    float (*expected_values)[2][4] = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            char symbol[64];
            snprintf(symbol, sizeof symbol, "generated_material_%d_%d", kind, waits);
            f->artifact->code = dlsym(library, symbol);
            assert(f->artifact->code);
            f->artifact->inspection_code = f->artifact->code;
            f->artifact->dispatch = NULL;
        }
        ecs_entity entity;
        ecs_world *world = whole_material_world(&entity);
        char label[32];
        strcpy(label, scenario == 4 ? "Missing" : "Value");
        float payload[4];
        pin_value args[] = { { .type = PIN_HANDLE,
                               .type_name = "ecs.entity",
                               .u64 = scenario == 2 ? UINT64_MAX
                                                    : ecs_entity_to_u64(entity) },
                             { .type = PIN_STR, .str = scenario == 3 ? NULL : label },
                             whole_material_value(kind, scenario == 5) };
        if (args[2].type == PIN_STRUCT) {
            memcpy(payload, args[2].ptr, sizeof payload);
            args[2].ptr = payload;
        }
        if (scenario == 6 && args[2].type == PIN_STRUCT) {
            args[2].ptr = NULL;
        }
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(bi, waits || step);
        if (scenario != 1) {
            assert(!behavior_instance_bind_service(bi, "ecs.world", world));
        }
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_run *run = flow_run_start(a->graph, a->graph->nodes[0], (flow_inputs){ args, 3 }, &options, NULL);
        assert(run);
        strcpy(label, "changed by caller");
        memset(payload, 0, sizeof payload);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = step ? flow_run_step(run) : flow_run_resume(run), at = 0;
        for (;;) {
            test_flow_snapshot s = test_flow_snapshot_take(run, bi, result);
            float values[32][4];
            render_shader_parameters(material, world, entity, NULL, values);
            if (!mode) {
                apush(expected, s);
                int index = acount(expected_values);
                asetlen(expected_values, index + 1);
                memcpy(expected_values[index], values, sizeof expected_values[index]);
            } else {
                assert(run->whole_function_executed && at < acount(expected));
                test_flow_snapshot_same(s, expected[at]);
                assert(!memcmp(expected_values[at], values, sizeof expected_values[at]));
                test_flow_snapshot_free(&s);
            }
            assert(++at < 40);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                if (!scenario && kind != 7 && budget == 100) {
                    assert(state == FLOW_COMPLETED);
                    pin_value reference = whole_material_value(kind, false);
                    for (int i = 0; i < 2; i++) {
                        if (reference.type == PIN_STRUCT) {
                            assert(
                                !memcmp(values[i], reference.ptr, (size_t)width * sizeof(float))
                            );
                        } else {
                            float expected = kind == 0 ? reference.f
                                           : kind == 1 ? (float)reference.d
                                                       : reference.b;
                            assert(values[i][0] == expected);
                        }
                    }
                }
                break;
            }
            result = state == FLOW_SUSPENDED
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
        render_shader_clear_world(world);
        ecs_world_free(world);
    }
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
    }
    afree(expected);
    afree(expected_values);
    f->artifact->code = code;
    f->artifact->inspection_code = inspection;
    f->artifact->dispatch = dispatch;
    behavior_asset_free(a);
}
void test_compiled_material(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    for (int kind = 0; kind < WHOLE_MATERIAL_KINDS; kind++) {
        for (int waits = 0; waits < 2; waits++) {
            for (int scenario = 0; scenario < 8; scenario++) {
                for (unsigned budget = 1; budget <= 7; budget++) {
                    parity(kind, waits, scenario, budget, false, library);
                }
                parity(kind, waits, scenario, 100, true, library);
            }
        }
    }
    if (library) {
        assert(!dlclose(library));
    }
    render_shader_catalog_clear();
    puts("PASS: whole material effects, scalar/vector payloads, error diagnostics, budgets, waits, stepping and watches");
}
