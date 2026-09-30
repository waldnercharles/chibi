#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_managed_fixture.h"
#include "test/whole_owned_array_fixture.h"

#include <dlfcn.h>
#include <time.h>

static behavior_function *entry(behavior_asset *a, graph *g)
{
    behavior_program *p = behavior_program_publish(a);
    const behavior_graph_program *program = behavior_program_graph(p, g);
    behavior_function *f = (behavior_function *)
        behavior_program_function(program, g->nodes[0]);
    if (!f) {
        const behavior_function_diagnostic
            *d = behavior_program_function_diagnostic(program, g->nodes[0]);
        fprintf(
            stderr,
            "owned array exclusion: %s graph=%s node=%s pin=%d\n",
            d->reason,
            d->graph->name,
            d->node->desc->name,
            d->pin
        );
    }
    assert(f);
    return f;
}
void emit_owned_array_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < OWNED_KINDS; kind++) {
        for (int waits = 0; waits < 2; waits++) {
            behavior_asset *a = whole_owned_array_fixture(kind, waits);
            char name[64];
            snprintf(name, sizeof name, "generated_owned_%d_%d", kind, waits);
            behavior_function_write_c(entry(a, a->graph), out, name);
            snprintf(name, sizeof name, "generated_owned_return_%d_%d", kind, waits);
            behavior_function_write_c(entry(a, a->graphs[1]), out, name);
            behavior_asset_free(a);
        }
    }
    assert(!fclose(out));
}
static void results(int kind, bool defaults, void *library)
{
    for (int mode = 0; mode < (library ? 3 : 2); mode++) {
        behavior_asset *a = whole_owned_array_fixture(kind, false);
        behavior_function *f = entry(a, a->graphs[1]);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        char label[] = "retained label", text[] = "retained nested text";
        char name[32];
        strcpy(name, (const char *[]){ "int", "const char *", "ManagedRecord" }[kind]);
        whole_managed record = { label, { text, 3.25f }, 7 };
        pin_value item = { .type = (pin_type[]){ PIN_I32, PIN_STR, PIN_STRUCT }[kind],
                           .type_name = name };
        if (kind == OWNED_INTS) {
            item.i = 42;
        } else if (kind == OWNED_STRINGS) {
            item.str = label;
        } else {
            item.ptr = &record;
        }
        pin_array input = { .element_type = item.type,
                            .type_name = name,
                            .items = &item,
                            .count = 1,
                            .capacity = 1 };
        pin_value arg = { .type = item.type,
                          .type_name = name,
                          .container = PIN_CONTAINER_ARRAY,
                          .ptr = &input },
                  out[2] = { 0 };
        behavior_function_context ctx = { .arguments = &arg,
                                          .argument_count = defaults ? 0 : 1,
                                          .results = out,
                                          .budget = 100 };
        arena storage = { 0 };
        behavior_function_bind_storage(&ctx, &storage);
        behavior_instance_bind_values(bi, &ctx);
        behavior_function_activation activation = { 0 };
        behavior_function_activation_init(f, &activation);
        ctx.activation = &activation;
        behavior_function_prepare_host(f, &ctx);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int status;
        if (mode == 2) {
            char symbol[64];
            snprintf(symbol, sizeof symbol, "generated_owned_return_%d_0", kind);
            int (*generated)(
                behavior_function_context *
            ) = (int (*)(behavior_function_context *))dlsym(library, symbol);
            assert(generated);
            status = generated(&ctx);
        } else {
            status = behavior_function_run(f, &ctx, mode == 1);
        }
        assert(!status && eval == eval_oracle_calls() && flow == flow_oracle_calls());
        behavior_function_release_host(&ctx);
        behavior_function_activation_free(&activation);
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
        memset(label, 'x', sizeof label - 1);
        memset(text, 'y', sizeof text - 1);
        memset(name, 'z', sizeof name - 1);
        memset(&record, 0, sizeof record);
        memset(&item, 0, sizeof item);
        memset(&input, 0, sizeof input);
        const pin_array *array = out[1].ptr;
        const char *expected_name = (const char *[]){ "int",
                                                      "const char *",
                                                      "ManagedRecord" }[kind];
        assert(out[1].container == PIN_CONTAINER_ARRAY && !strcmp(out[1].type_name, expected_name));
        assert(array && array->count == (defaults ? 3 : 1));
        assert(!strcmp(array->type_name, expected_name));
        assert(!strcmp(array->items[0].type_name, expected_name));
        if (kind == OWNED_INTS) {
            assert(array->items[0].i == (defaults ? 1 : 42));
        } else if (kind == OWNED_STRINGS) {
            assert(!strcmp(array->items[0].str, defaults ? "one" : "retained label"));
        } else {
            const whole_managed *actual = array->items[0].ptr;
            assert(actual && !strcmp(actual->label, defaults ? "one" : "retained label"));
            assert(!strcmp(actual->nested.text, defaults ? "first" : "retained nested text"));
            assert(actual->nested.weight == (defaults ? 1.25f : 3.25f));
            assert(actual->count == (defaults ? 1 : 7));
        }
        arena_free(&storage);
    }
}
static void variable_defaults()
{
    for (int kind = 0; kind < 2; kind++) {
        pin_type type = (pin_type[]){ PIN_STR, PIN_NAME }[kind];
        behavior_asset *a = behavior_asset_new("Array variable defaults");
        char text[] = "owned default";
        pin_value item = { .type = type, .str = text };
        pin_array array = { .element_type = type, .items = &item, .count = 1, .capacity = 1 };
        pin_value value = { .type = type, .container = PIN_CONTAINER_ARRAY, .ptr = &array };
        behavior_variable *
            v = behavior_asset_add_variable(a, "Values", type, NULL, PIN_CONTAINER_ARRAY, &value, "");
        assert(v && !strcmp(((pin_array *)v->default_value.ptr)->items[0].str, text));
        assert(!behavior_asset_set_variable_default(a, v->id, &value));
        memset(text, 'x', sizeof text - 1);
        assert(!strcmp(((pin_array *)v->default_value.ptr)->items[0].str, "owned default"));
        assert(!behavior_asset_set_variable_container(a, v->id, PIN_CONTAINER_ARRAY));
        assert(((pin_array *)v->default_value.ptr)->count == 1);
        assert(!behavior_asset_set_variable_container(a, v->id, PIN_CONTAINER_SINGLE));
        assert(
            v->default_value.container == PIN_CONTAINER_SINGLE &&
            !strcmp(v->default_value.str, "")
        );
        assert(!behavior_asset_set_variable_container(a, v->id, PIN_CONTAINER_ARRAY));
        assert(
            v->default_value.container == PIN_CONTAINER_ARRAY &&
            !v->default_value.ptr
        );
        behavior_asset_free(a);
    }
}
static void parity(int kind, bool waits, int scenario, int control, int budget, void *library)
{
    behavior_asset *a = whole_owned_array_fixture(kind, waits);
    graph *hold = a->graphs[1];
    assert(!graph_set_breakpoint(hold, hold->nodes[1], control == 3));
    behavior_function *f = entry(a, a->graph);
    void *code = f->code, *inspection = f->inspection_code, *dispatch = f->dispatch;
    test_flow_snapshot *expected = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            char name[64];
            snprintf(name, sizeof name, "generated_owned_%d_%d", kind, waits);
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
        behavior_instance_set_trace_enabled(bi, control != 0);
        behavior_instance_set_breakpoints_enabled(bi, control == 3);
        pin_value args[] = { a->graph->nodes[0]->output_pins[1].default_value,
                             { .type = PIN_BOOL, .b = scenario != 2 } };
        pin_array empty = { .element_type = args[0].type,
                            .type_name = args[0].type_name };
        if (scenario == 3) {
            args[0].ptr = NULL;
        } else if (scenario == 4) {
            args[0].ptr = &empty;
        }
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_run *run = flow_run_start(
            a->graph,
            a->graph->nodes[0],
            (flow_inputs){ args, scenario ? 2 : 0 },
            &options,
            NULL
        );
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
            assert(++at < 128);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                if (budget == 100 && scenario != 3) {
                    assert(state == FLOW_COMPLETED);
                    assert(bi->variables[4].i == (scenario == 4 ? 0 : 3));
                }
                break;
            }
            if (state == FLOW_SUSPENDED) {
                pin_value replacement = args[0];
                replacement.ptr = NULL;
                assert(!behavior_instance_set_variable(bi, a->variables[0].id, &replacement));
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
void test_owned_arrays(const char *path)
{
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    variable_defaults();
    for (int kind = 0; kind < OWNED_KINDS; kind++) {
        results(kind, false, library);
        results(kind, true, library);
        for (int waits = 0; waits < 2; waits++) {
            for (int scenario = 0; scenario < 5; scenario++) {
                parity(kind, waits, scenario, 0, 100, library);
                parity(kind, waits, scenario, 1, 100, library);
            }
            for (int budget = 1; budget < 5; budget++) {
                parity(kind, waits, 0, 0, budget, library);
            }
            parity(kind, waits, 0, 2, 100, library);
            parity(kind, waits, 0, 3, 100, library);
        }
    }
    if (library) {
        dlclose(library);
    }
    test_note("PASS: whole owned array defaults, returns, persistent stores, iteration snapshots, nested waits, cancellation and inspection\n");
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_owned_arrays(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int kind = 0; kind < OWNED_KINDS; kind++) {
        behavior_asset *a = whole_owned_array_fixture(kind, false);
        behavior_function *f = entry(a, a->graph);
        char name[64];
        snprintf(name, sizeof name, "generated_owned_%d_0", kind);
        int (*generated)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))dlsym(library, name);
        int (*native)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))behavior_function_entry(f);
        assert(generated && native);
        for (int order = 0; order < 3; order++) {
            int mode = (order + rotation) % 3;
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            behavior_function_context ctx = { .budget = 100 };
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
            assert(bi->variables[4].i == (count + 64) * 3);
            assert(
                ((pin_array *)bi->variables[2].ptr)->count == 3 &&
                !bi->variables[1].ptr
            );
            printf(
                "%s owned_%s ns=%.3f checksum=%d\n",
                (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C" }[mode],
                (const char *[]){ "ints", "strings", "structs" }[kind],
                elapsed * 1e9 / count,
                bi->variables[4].i
            );
            behavior_function_release_host(&ctx);
            behavior_function_activation_free(&activation);
            arena_free(&storage);
            behavior_instance_destroy(bi);
        }
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    return 0;
}
