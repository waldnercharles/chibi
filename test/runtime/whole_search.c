#include "graph/authoring/graph_edit.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_search_fixture.h"

#include <dlfcn.h>
#include <math.h>
#include <time.h>

static const pin_type types[] = { PIN_BOOL,   PIN_I8,  PIN_U8,  PIN_I16,
                                  PIN_U16,    PIN_I32, PIN_U32, PIN_I64,
                                  PIN_U64,    PIN_F32, PIN_F64, PIN_ENUM,
                                  PIN_HANDLE, PIN_STR, PIN_NAME };
static pin_value value(pin_type type, int number)
{
    pin_value v = { .type = type };
    switch (type) {
        case PIN_BOOL: v.b = number != 0; break;
        case PIN_I8: v.i8 = number; break;
        case PIN_U8: v.u8 = number; break;
        case PIN_I16: v.i16 = number; break;
        case PIN_U16: v.u16 = number; break;
        case PIN_I32: v.i = number; break;
        case PIN_U32: v.u = number; break;
        case PIN_I64: v.i64 = INT64_MIN + number; break;
        case PIN_U64: v.u64 = UINT64_MAX - number; break;
        case PIN_F32: v.f = number; break;
        case PIN_F64: v.d = number; break;
        case PIN_ENUM:
            v.e = number;
            v.type_name = "SearchKind";
            break;
        case PIN_HANDLE:
            v.u64 = UINT64_MAX - number;
            v.type_name = "ecs.entity";
            break;
        case PIN_STR:
        case PIN_NAME:
            v.str = number ? "needle \xE2\x98\x83" : "haystack";
            break;
        default: assert(false);
    }
    return v;
}
static void equality()
{
    char first[] = "same type", second[] = "same type";
    for (int i = 0; i < (int)(sizeof types / sizeof *types); i++) {
        pin_value original = value(types[i], 1), a, b;
        memset(&a, 0x55, sizeof a);
        memset(&b, 0xaa, sizeof b);
        a.type = b.type = original.type;
        a.container = b.container = PIN_CONTAINER_SINGLE;
        a.type_name = first;
        b.type_name = second;
        memcpy(&a.d, &original.d, behavior_scalar_size(original.type));
        memcpy(&b.d, &original.d, behavior_scalar_size(original.type));
        assert(pin_value_equal(&a, &b));
        if (types[i] == PIN_ENUM || types[i] == PIN_HANDLE) {
            b.type_name = "different type";
            assert(!pin_value_equal(&a, &b));
        }
    }
    pin_value a = { .type = PIN_F32, .f = -0.0f }, b = { .type = PIN_F32, .f = 0.0f };
    assert(pin_value_equal(&a, &b));
    a.f = b.f = NAN;
    assert(!pin_value_equal(&a, &b));
    a = (pin_value){ .type = PIN_F64, .d = -0.0 };
    b = (pin_value){ .type = PIN_F64, .d = 0.0 };
    assert(pin_value_equal(&a, &b));
    a.d = b.d = NAN;
    assert(!pin_value_equal(&a, &b));
}
static void array_defaults()
{
    for (int type = PIN_NAME; type <= PIN_STR; type++) {
        behavior_asset *a = whole_search_fixture(type, false, false);
        graph *g = a->graphs[1];
        node *entry = g->nodes[0];
        assert(!entry->output_pins[0].default_value.ptr);
        char text[] = "array default";
        pin_value item = { .type = type, .str = text };
        pin_array array = { .element_type = type, .items = &item, .count = 1, .capacity = 1 };
        assert(!graph_set_pin_default(
            g,
            entry,
            1,
            0,
            &(pin_value){ .type = type, .container = PIN_CONTAINER_ARRAY, .ptr = &array }
        ));
        memset(text, 'x', sizeof text - 1);
        pin_array *owned = entry->output_pins[0].default_value.ptr;
        assert(owned != &array && owned->count == 1 && !strcmp(owned->items[0].str, "array default"));
        behavior_asset_free(a);
    }
}
static behavior_function *entry(behavior_asset *a, graph *g)
{
    behavior_program *program = behavior_program_publish(a);
    const behavior_graph_program *p = behavior_program_graph(program, g);
    if (!p) {
        fprintf(
            stderr,
            "search publication: %s\n",
            behavior_program_diagnostic(program)->message
        );
    }
    assert(p);
    behavior_function *f = (behavior_function *)behavior_program_function(p, g->nodes[0]);
    if (!f) {
        const behavior_function_diagnostic
            *d = behavior_program_function_diagnostic(p, g->nodes[0]);
        fprintf(
            stderr,
            "search exclusion: %s graph=%s node=%s pin=%d\n",
            d->reason,
            d->graph->name,
            d->node->desc->name,
            d->pin
        );
    }
    assert(f && f->custom_lowering);
    return f;
}
void emit_search_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int t = 0; t < (int)(sizeof types / sizeof *types); t++) {
        for (int contains = 0; contains < 2; contains++) {
            for (int waits = 0; waits < 2; waits++) {
                behavior_asset *a = whole_search_fixture(types[t], contains, waits);
                char name[64];
                snprintf(name, sizeof name, "generated_search_%d_%d_%d", types[t], contains, waits);
                behavior_function_write_c(entry(a, a->graph), out, name);
                if (!waits) {
                    snprintf(name, sizeof name, "generated_lookup_%d_%d", types[t], contains);
                    behavior_function_write_c(entry(a, a->graphs[1]), out, name);
                }
                behavior_asset_free(a);
            }
        }
    }
    behavior_asset *loop = whole_search_loop_fixture();
    behavior_function_write_c(entry(loop, loop->graph), out, "generated_search_loop");
    behavior_asset_free(loop);
    assert(!fclose(out));
}
static void parity(pin_type type, bool contains, bool waits, int scenario, int control, int budget, void *library)
{
    behavior_asset *a = scenario == 10
                          ? whole_search_loop_fixture()
                          : whole_search_fixture(type, contains, waits);
    graph *apply = a->graphs[2];
    assert(!graph_set_breakpoint(apply, apply->nodes[1], control == 3));
    behavior_function *f = entry(a, a->graph);
    void *code = f->artifact->code, *inspection = f->artifact->inspection_code,
         *dispatch = f->artifact->dispatch;
    if (type <= PIN_F64) {
        for (int pc = 0; pc < acount(f->instructions); pc++) {
            assert(!behavior_function_call(f->instructions[pc].op));
        }
    }
    test_flow_snapshot *expected = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            char name[64];
            snprintf(name, sizeof name, "generated_search_%d_%d_%d", type, contains, waits);
            f->artifact->code = dlsym(library, scenario == 10 ? "generated_search_loop" : name);
            assert(f->artifact->code);
            f->artifact->inspection_code = f->artifact->code;
            f->artifact->dispatch = NULL;
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
        pin_value items[33];
        for (int i = 0; i < 33; i++) {
            items[i] = value(type, 0);
        }
        pin_value needle = value(type, 1);
        int found = scenario < 2 ? (scenario ? 32 : 0) : -1;
        if (found >= 0) {
            items[found] = needle;
            if (!found) {
                items[32] = needle;
            }
        }
        if (scenario == 5 && (type == PIN_STR || type == PIN_NAME)) {
            needle.str = NULL;
            items[32].str = "";
            found = 32;
        }
        if (scenario == 5 && (type == PIN_ENUM || type == PIN_HANDLE)) {
            items[32] = needle;
            items[32].type_name = "different type";
        }
        if ((scenario == 5 || scenario == 6) && (type == PIN_F32 || type == PIN_F64)) {
            needle = value(type, 0);
            if (type == PIN_F32) {
                needle.f = scenario == 5 ? -0.0f : NAN;
            } else {
                needle.d = scenario == 5 ? -0.0 : NAN;
            }
            found = scenario == 5 ? 0 : -1;
        }
        if (scenario == 7) {
            items[0] = needle;
            items[0].type = type == PIN_I32 ? PIN_U32 : PIN_I32;
            items[0].i = 1;
        }
        if (scenario == 8) {
            items[0] = (pin_value){ .type = type, .container = PIN_CONTAINER_ARRAY };
        }
        if (scenario == 10) {
            for (int i = 0; i < 5; i++) {
                items[i].i = (int[]){ 3, 1, 2, 4, 0 }[i];
            }
            found = 0;
        }
        pin_array array = { .element_type = type,
                            .type_name = needle.type_name,
                            .count = scenario == 3 ? 0 : 33,
                            .capacity = 33,
                            .items = items };
        if (scenario == 9) {
            array.type_name = "";
        }
        pin_value args[] = { { .type = type,
                               .type_name = needle.type_name,
                               .container = PIN_CONTAINER_ARRAY,
                               .ptr = scenario == 4 ? NULL : &array },
                             needle };
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_run *run = flow_run_start(a->graph, a->graph->nodes[0], (flow_inputs){ args, 2 }, &options, NULL);
        assert(run);
        for (int i = 0; i < 33; i++) {
            items[i] = value(type, 0);
        }
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = control == 1 ? flow_run_step(run) : flow_run_resume(run), at = 0;
        for (;;) {
            test_flow_snapshot snapshot = test_flow_snapshot_take(run, bi, result);
            if (!mode) {
                apush(expected, snapshot);
            } else {
                assert(run->whole_function_executed && at < acount(expected));
                if (acount(snapshot.leaf_traversals) !=
                    acount(expected[at].leaf_traversals)) {
                    fprintf(
                        stderr,
                        "search trace mismatch scenario=%d control=%d mode=%d "
                        "at=%d node=%llu leaf=%d/%d\n",
                        scenario,
                        control,
                        mode,
                        at,
                        (unsigned long long)snapshot.node,
                        acount(snapshot.leaf_traversals),
                        acount(expected[at].leaf_traversals)
                    );
                }
                test_flow_snapshot_same(snapshot, expected[at]);
                test_flow_snapshot_free(&snapshot);
            }
            assert(++at < 64);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                if (budget == 100) {
                    assert(state == FLOW_COMPLETED);
                    bool correct = contains
                                     ? bi->execution.variables[0].b == (found >= 0)
                                     : bi->execution.variables[0].i == found;
                    if (!correct) {
                        fprintf(
                            stderr,
                            "search mismatch type=%d contains=%d waits=%d "
                            "scenario=%d control=%d mode=%d actual=%d "
                            "expected=%d\n",
                            type,
                            contains,
                            waits,
                            scenario,
                            control,
                            mode,
                            contains ? bi->execution.variables[0].b
                                     : bi->execution.variables[0].i,
                            contains ? found >= 0 : found
                        );
                    }
                    assert(correct);
                }
                break;
            }
            if (state == FLOW_SUSPENDED && control == 2) {
                assert(!flow_run_stop(run));
                break;
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
    f->artifact->code = code;
    f->artifact->inspection_code = inspection;
    f->artifact->dispatch = dispatch;
    behavior_asset_free(a);
}
void test_compiled_search(const char *path)
{
    equality();
    array_defaults();
    whole_search_smoke();
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    for (int t = 0; t < (int)(sizeof types / sizeof *types); t++) {
        for (int contains = 0; contains < 2; contains++) {
            for (int waits = 0; waits < 2; waits++) {
                for (int scenario = 0; scenario < 10; scenario++) {
                    parity(types[t], contains, waits, scenario, 0, 100, library);
                    parity(types[t], contains, waits, scenario, 1, 100, library);
                }
                for (int budget = 1; budget <= 4; budget++) {
                    parity(types[t], contains, waits, 1, 0, budget, library);
                }
                parity(types[t], contains, waits, 1, 2, 100, library);
                parity(types[t], contains, waits, 1, 3, 100, library);
            }
        }
    }
    for (int control = 0; control < 4; control++) {
        parity(PIN_I32, false, false, 10, control, 100, library);
    }
    for (int budget = 1; budget <= 6; budget++) {
        parity(PIN_I32, false, false, 10, 0, budget, library);
    }
    if (library) {
        assert(!dlclose(library));
    }
    puts("PASS: compiled typed Contains/Find, first/last/miss/empty, nominal equality, signed zero/NaN, wait snapshots and debugger parity");
}
static double seconds()
{
    struct timespec time;
    assert(!clock_gettime(CLOCK_MONOTONIC, &time));
    return time.tv_sec + time.tv_nsec * 1e-9;
}
int bench_search(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    const pin_type measured[] = { PIN_I32, PIN_F64, PIN_HANDLE, PIN_STR };
    const int sizes[] = { 0, 8, 128, 4096 };
    for (int t = 0; t < 4; t++) {
        pin_type type = measured[t];
        behavior_asset *a = whole_search_fixture(type, false, false);
        const behavior_function *f = entry(a, a->graphs[1]);
        char name[64];
        snprintf(name, sizeof name, "generated_lookup_%d_0", type);
        int (*generated)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))dlsym(library, name);
        int (*native)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))behavior_function_entry(f);
        assert(generated && native);
        pin_value items[4096];
        for (int i = 0; i < 4096; i++) {
            items[i] = value(type, 0);
        }
        for (int size = 0; size < 4; size++) {
            for (int order = 0; order < 3; order++) {
                int mode = (order + rotation) % 3;
                pin_array array = { .element_type = type,
                                    .count = sizes[size],
                                    .capacity = 4096,
                                    .items = items };
                pin_value args[] = { { .type = type,
                                       .container = PIN_CONTAINER_ARRAY,
                                       .ptr = &array },
                                     value(type, 1) };
                pin_value result = { .type = PIN_I32 };
                behavior_function_context ctx = { .arguments = args,
                                                  .argument_count = 2,
                                                  .results = &result,
                                                  .budget = 100 };
                arena storage = { 0 };
                behavior_function_bind_storage(&ctx, &storage);
                behavior_function_activation activation = { 0 };
                behavior_function_activation_init(f, &activation);
                ctx.activation = &activation;
                behavior_function_prepare_host(f, &ctx);
                uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(),
                         data = 0, vm = 0;
                int iterations = count / (sizes[size] > 128 ? 64 : 1);
                assert(iterations > 0);
                double start = 0;
                for (int i = -64; i < iterations; i++) {
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
                    assert(!status && result.i == -1);
                }
                double elapsed = seconds() - start;
                assert(
                    data == behavior_function_data_allocations() &&
                    vm == behavior_function_vm_allocations()
                );
                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
                printf(
                    "%s search_%d_%d ns=%.3f checksum=%d\n",
                    (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C" }[mode],
                    type,
                    sizes[size],
                    elapsed * 1e9 / iterations,
                    result.i
                );
                behavior_function_release_host(&ctx);
                behavior_function_activation_free(&activation);
                arena_free(&storage);
            }
        }
        behavior_asset_free(a);
    }
    assert(!dlclose(library));
    return 0;
}
