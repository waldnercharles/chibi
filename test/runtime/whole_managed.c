#include "ecs/execution.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_managed_fixture.h"

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
            "managed exclusion: %s graph=%s node=%llu pin=%d\n",
            d->reason,
            d->graph->name,
            (unsigned long long)d->node->id,
            d->pin
        );
    }
    assert(f);
    return f;
}
void emit_managed_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int waits = 0; waits < 2; waits++) {
        for (int components = 0; components < 2; components++) {
            behavior_asset *a = whole_managed_fixture(waits, components);
            char name[64];
            snprintf(name, sizeof name, "generated_managed_%d_%d", waits, components);
            behavior_function_write_c(entry(a), out, name);
            behavior_asset_free(a);
        }
    }
    behavior_asset *a = whole_managed_fixture(false, false);
    behavior_program *p = behavior_program_publish(a);
    graph *hold = a->graphs[2];
    behavior_function_write_c(
        behavior_program_function(behavior_program_graph(p, hold), hold->nodes[0]),
        out,
        "generated_managed_return"
    );
    behavior_asset_free(a);
    assert(!fclose(out));
}
static void results(void *library)
{
    for (int mode = 0; mode < (library ? 3 : 2); mode++) {
        behavior_asset *a = whole_managed_fixture(false, false);
        graph *hold = a->graphs[2];
        behavior_program *p = behavior_program_publish(a);
        const behavior_function *f = behavior_program_function(
            behavior_program_graph(p, hold),
            hold->nodes[0]
        );
        assert(f);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        char label[] = "returned label", text[] = "returned nested text",
             name[] = "ManagedRecord";
        whole_managed record = { label, { text, 3.25f }, 7 };
        pin_value input = { .type = PIN_STRUCT, .type_name = name, .ptr = &record },
                  out[3] = { 0 };
        behavior_function_context ctx = { .arguments = &input,
                                          .argument_count = 1,
                                          .results = out,
                                          .budget = 100 };
        arena storage = { 0 };
        behavior_function_prepare_results(f, out, &storage);
        behavior_function_bind_storage(&ctx, &storage);
        behavior_instance_bind_values(bi, &ctx);
        behavior_function_activation activation = { 0 };
        behavior_function_activation_init(f, &activation);
        ctx.activation = &activation;
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int status;
        if (mode == 2) {
            int (*generated)(behavior_function_context *) = (int (*)(
                behavior_function_context *
            ))dlsym(library, "generated_managed_return");
            assert(generated);
            status = generated(&ctx);
        } else {
            status = behavior_function_run(f, &ctx, mode == 1);
        }
        assert(!status && eval == eval_oracle_calls() && flow == flow_oracle_calls());
        behavior_function_activation_free(&activation);
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
        memset(label, 'x', sizeof label - 1);
        memset(text, 'y', sizeof text - 1);
        memset(name, 'z', sizeof name - 1);
        memset(&record, 0, sizeof record);
        const whole_managed *actual = out[1].ptr;
        assert(actual && !strcmp(actual->label, "returned label"));
        assert(!strcmp(actual->nested.text, "returned nested text"));
        assert(actual->nested.weight == 3.25f && actual->count == 7);
        assert(!strcmp(out[1].type_name, "ManagedRecord"));
        assert(!strcmp(out[2].str, "returned label"));
        arena_free(&storage);
    }
}
static bool same_text(const char *a, const char *b)
{
    return a && b ? !strcmp(a, b) : a == b;
}
static void same_record(const whole_managed *a, const whole_managed *b)
{
    assert(same_text(a->label, b->label) && same_text(a->nested.text, b->nested.text));
    assert(a->nested.weight == b->nested.weight && a->count == b->count);
}
static void parity(bool waits, bool components, int scenario, int control, int budget, void *library)
{
    behavior_asset *a = whole_managed_fixture(waits, components);
    graph *hold = a->graphs[2];
    assert(!graph_set_breakpoint(hold, hold->nodes[1], control == 3));
    behavior_function *f = entry(a);
    void *code = f->artifact->code, *inspection = f->artifact->inspection_code,
         *dispatch = f->artifact->dispatch;
    test_flow_snapshot *expected = NULL;
    pin_value *states = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            char name[64];
            snprintf(name, sizeof name, "generated_managed_%d_%d", waits, components);
            f->artifact->code = dlsym(library, name);
            assert(f->artifact->code);
            f->artifact->inspection_code = f->artifact->code;
            f->artifact->dispatch = NULL;
        }
        ecs_world *world = ecs_world_new();
        assert(!ecs_world_register_component(world, &WHOLE_MANAGED_COMPONENT, NULL));
        ecs_entity entity = ecs_world_create_entity(world);
        if (scenario != 5) {
            assert(!ecs_world_record_add(world, entity, WHOLE_MANAGED_COMPONENT.id, NULL, 0));
            assert(!ecs_world_apply_commands(world));
        }
        pin_value owner = { .type = PIN_HANDLE,
                            .type_name = "ecs.entity",
                            .u64 = ecs_entity_to_u64(entity) };
        behavior_instance *bi = behavior_instance_create(a, &owner, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (
                behavior_backend[]
            ){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(bi, control != 0);
        behavior_instance_set_breakpoints_enabled(bi, control == 3);
        if (scenario != 4) {
            assert(!behavior_instance_bind_service(bi, "ecs.world", world));
        }
        char label[] = "retained label",
             text[] = "nested \"UTF-8\" \xE2\x98\x83";
        whole_managed record = { label, { text, -3.25f }, 7 };
        if (scenario == 1) {
            record.label = NULL;
            record.nested.text = NULL;
        }
        if (scenario == 2) {
            record.label = "";
            record.nested.text = label;
        }
        pin_value arg = { .type = PIN_STRUCT,
                          .type_name = "ManagedRecord",
                          .ptr = scenario == 3 ? NULL : &record };
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_run *run = flow_run_start(
            a->graph,
            a->graph->nodes[0],
            (flow_inputs){ &arg, 1 },
            &options,
            NULL
        );
        assert(run);
        memset(label, 'x', sizeof label - 1);
        memset(text, 'y', sizeof text - 1);
        record.count = 999;
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = control == 1 ? flow_run_step(run) : flow_run_resume(run), at = 0;
        for (;;) {
            test_flow_snapshot snapshot = test_flow_snapshot_take(run, bi, result);
            const whole_managed *component = ecs_component_data_const(
                world,
                entity,
                WHOLE_MANAGED_COMPONENT.id
            );
            if (!mode) {
                pin_value state = { .type = PIN_STRUCT,
                                    .type_name = "ManagedRecord",
                                    .ptr = (void *)component },
                          copy;
                assert(!pin_value_snapshot_to_arena(&snapshot.storage, &copy, &state));
                apush(states, copy);
                apush(expected, snapshot);
            } else {
                assert(run->whole_function_executed && at < acount(expected));
                test_flow_snapshot_same(snapshot, expected[at]);
                test_flow_snapshot_free(&snapshot);
                assert((component == NULL) == (states[at].ptr == NULL));
                if (component) {
                    same_record(component, states[at].ptr);
                }
            }
            assert(++at < 64);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                if (scenario < 3 && budget == 100) {
                    assert(state == FLOW_COMPLETED);
                    assert(((whole_managed *)bi->execution.variables[1].ptr)->count == 7);
                }
                break;
            }
            if (state == FLOW_SUSPENDED) {
                whole_managed replacement = { "replaced", { "replacement", 13 }, 23 };
                pin_value v = { .type = PIN_STRUCT,
                                .type_name = "ManagedRecord",
                                .ptr = &replacement };
                assert(!behavior_instance_set_variable(bi, a->variables[0].id, &v));
                if (control == 2) {
                    assert(!flow_run_stop(run));
                    break;
                }
            }
            result = state == FLOW_SUSPENDED
                       ? flow_run_tick_at(run, flow_run_wake_time(run))
                       : flow_run_step(run);
        }
        if (mode) {
            assert(at == acount(expected));
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        }
        flow_run_free(run);
        behavior_instance_destroy(bi);
        const whole_managed *component = ecs_component_data_const(
            world,
            entity,
            WHOLE_MANAGED_COMPONENT.id
        );
        if (component) {
            same_record(component, states[at - 1].ptr);
        }
        ecs_world_free(world);
    }
    for (int i = 0; i < acount(expected); i++) {
        test_flow_snapshot_free(&expected[i]);
    }
    afree(states);
    afree(expected);
    f->artifact->code = code;
    f->artifact->inspection_code = inspection;
    f->artifact->dispatch = dispatch;
    behavior_asset_free(a);
}
void test_compiled_managed(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    results(library);
    for (int waits = 0; waits < 2; waits++) {
        for (int components = 0; components < 2; components++) {
            for (int scenario = 0; scenario < 6; scenario++) {
                parity(waits, components, scenario, 0, 100, library);
                parity(waits, components, scenario, 1, 100, library);
            }
            for (int budget = 1; budget <= 4; budget++) {
                parity(waits, components, 0, 0, budget, library);
            }
            parity(waits, components, 0, 2, 100, library);
            parity(waits, components, 0, 3, 100, library);
        }
    }
    if (library) {
        assert(!dlclose(library));
    }
    puts("PASS: whole managed structs, strings, retention across nested waits, variable overwrite, component lifetime, errors and inspection");
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_managed(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int components = 0; components < 2; components++) {
        behavior_asset *a = whole_managed_fixture(false, components);
        behavior_function *f = entry(a);
        char name[64];
        snprintf(name, sizeof name, "generated_managed_0_%d", components);
        int (*generated)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))dlsym(library, name);
        int (*native)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))behavior_function_entry(f);
        assert(generated && native);
        for (int order = 0; order < 3; order++) {
            int mode = (order + rotation) % 3;
            ecs_world *world = ecs_world_new();
            assert(!ecs_world_register_component(world, &WHOLE_MANAGED_COMPONENT, NULL));
            ecs_entity entity = ecs_world_create_entity(world);
            assert(!ecs_world_record_add(world, entity, WHOLE_MANAGED_COMPONENT.id, NULL, 0));
            assert(!ecs_world_apply_commands(world));
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            whole_managed record = { "measured label",
                                     { "measured nested text", 3.25f },
                                     7 };
            pin_value arg = { .type = PIN_STRUCT,
                              .type_name = "ManagedRecord",
                              .ptr = &record };
            behavior_function_context ctx = { .arguments = &arg,
                                              .argument_count = 1,
                                              .budget = 100,
                                              .world = (const ecs_execution_view *)world,
                                              .owner = ecs_entity_to_u64(entity) };
            behavior_instance_bind_values(bi, &ctx);
            arena storage = { 0 };
            behavior_function_bind_storage(&ctx, &storage);
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
                arena_reset(&storage);
                ctx.steps = 0;
                int status = mode == 2 ? generated(&ctx)
                           : mode == 0 ? native(&ctx)
                                       : behavior_function_run(f, &ctx, false);
                assert(!status);
            }
            double elapsed = seconds() - start;
            assert(
                data == behavior_function_data_allocations() &&
                vm == behavior_function_vm_allocations()
            );
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            same_record(bi->execution.variables[1].ptr, &record);
            assert(!strcmp(bi->execution.variables[2].str, record.label));
            if (components) {
                same_record(
                    ecs_component_data_const(
                        world,
                        entity,
                        WHOLE_MANAGED_COMPONENT.id
                    ),
                    &record
                );
            }
            printf(
                "%s managed_%s ns=%.3f checksum=%d\n",
                (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C" }[mode],
                components ? "component" : "records",
                elapsed * 1e9 / count,
                ((whole_managed *)bi->execution.variables[1].ptr)->count
            );
            behavior_function_release_host(&ctx);
            behavior_function_activation_free(&activation);
            arena_free(&storage);
            behavior_instance_destroy(bi);
            ecs_world_free(world);
        }
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    return 0;
}
