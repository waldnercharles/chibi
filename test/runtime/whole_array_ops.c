#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_array_op_fixture.h"
#include "test/whole_managed_fixture.h"

#include <dlfcn.h>
#include <time.h>

static behavior_function *body(behavior_asset *a)
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
            "array operation exclusion: %s graph=%s node=%s pin=%d\n",
            d->reason,
            d->graph->name,
            d->node->desc->name,
            d->pin
        );
    }
    assert(f);
    return f;
}
void emit_array_ops_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < ARRAY_OP_KINDS; kind++) {
        for (int op = 0; op < ARRAY_OP_COUNT; op++) {
            for (int waits = 0; waits < 2; waits++) {
                behavior_asset *a = whole_array_op_fixture(kind, op, waits);
                char name[64];
                snprintf(name, sizeof name, "generated_array_op_%d_%d_%d", kind, op, waits);
                behavior_function_write_c(body(a), out, name);
                if (op >= ARRAY_OP_MAKE_SET && !waits) {
                    graph *g = a->graphs[1];
                    const behavior_function *f = behavior_program_function(
                        behavior_program_graph(a->program, g),
                        g->nodes[0]
                    );
                    assert(f);
                    snprintf(name, sizeof name, "generated_set_return_%d_%d", kind, op);
                    behavior_function_write_c(f, out, name);
                }
                behavior_asset_free(a);
            }
        }
    }
    assert(!fclose(out));
}
static int item_id(const pin_value *value)
{
    if (value->type == PIN_I32) {
        return value->i;
    }
    if (value->type == PIN_STRUCT) {
        const whole_managed *v = value->ptr;
        assert(v);
        return v->count;
    }
    const char *names[] = { "one", "two", "three", "four" };
    for (int i = 0; i < 4; i++) {
        if (value->str && !strcmp(value->str, names[i])) {
            return i + 1;
        }
    }
    assert(!value->str);
    return 0;
}
static void expected_default(const pin_value *value, int op)
{
    static const int items[ARRAY_OP_COUNT][6] = {
        { 2, 2, 2 },    { 0 },          { 0 },
        { 1, 2, 3, 2 }, { 1, 2, 3 },    { 1, 2, 3, 3, 4 },
        { 1, 2, 3 },    { 1, 2, 2, 3 }, { 1, 3 },
        { 1, 3 },       { 0 },          { 3, 2, 1 },
        { 1, 3, 2 },    { 1 },          { 0 },
        { 0 },          { 2 },          { 1, 2, 3 },
        { 0 },          { 0 }
    };
    static const int counts[] = { 3, 0, 0, 4, 3, 5, 3, 4, 2, 2,
                                  0, 3, 3, 1, 0, 0, 1, 3, 0, 0 };
    if (op == ARRAY_OP_GET) {
        assert(item_id(value) == 2);
        return;
    }
    if (op == ARRAY_OP_IDENTICAL || op == ARRAY_OP_CONTAINS ||
        op == ARRAY_OP_SET_CONTAINS) {
        assert(value->type == PIN_BOOL && value->b == (op != ARRAY_OP_IDENTICAL));
        return;
    }
    if (op == ARRAY_OP_FIND) {
        assert(value->type == PIN_I32 && value->i == 1);
        return;
    }
    if (op == ARRAY_OP_SET_LENGTH) {
        assert(value->type == PIN_I32 && value->i == 3);
        return;
    }
    const pin_array *array = value->ptr;
    assert(
        value->container ==
            (op >= ARRAY_OP_MAKE_SET ? PIN_CONTAINER_SET : PIN_CONTAINER_ARRAY) &&
        array && array->count == counts[op]
    );
    for (int i = 0; i < array->count; i++) {
        assert(
            array->items[i].type == value->type &&
            item_id(&array->items[i]) == items[op][i]
        );
    }
}
static void scenario_inputs(int scenario, pin_value args[5], pin_array *empty)
{
    if (scenario == 1) {
        args[3].i = -1;
        args[4].i = 0;
    } else if (scenario == 2) {
        args[3].i = 0;
        args[4].i = 5;
    } else if (scenario == 3) {
        args[3].i = 5;
    } else if (scenario == 4) {
        *empty = *(pin_array *)args[0].ptr;
        empty->count = 0;
        args[0].ptr = empty;
    } else if (scenario == 5) {
        args[0].ptr = NULL;
    } else if (scenario == 6) {
        args[2] = ((pin_array *)args[1].ptr)->items[1];
    } else if (scenario == 7) {
        args[1] = args[0];
    } else if (scenario == 8) {
        if (args[2].type == PIN_STRUCT) {
            args[2].ptr = NULL;
        } else if (args[2].type == PIN_STR || args[2].type == PIN_NAME) {
            args[2].str = NULL;
        }
    }
}
static void expected_growth(const pin_value *value)
{
    const pin_array *array = value->ptr;
    assert(value->container == PIN_CONTAINER_ARRAY && array && array->count == 5);
    for (int i = 0; i < 5; i++) {
        const pin_value *item = &array->items[i];
        assert(item->type == value->type && !strcmp(item->type_name, value->type_name));
        if (i < 3) {
            assert(item_id(item) == i + 1);
            continue;
        }
        if (item->type == PIN_I32) {
            assert(item->i == 0);
        } else {
            assert(!item->ptr);
        }
    }
}
static void helper_ownership(int kind, int op)
{
    behavior_asset *a = whole_array_op_fixture(kind, op, false);
    const node *operation = a->graphs[1]->nodes[2];
    if (!operation->desc->host_call || !operation->desc->host_call->value) {
        behavior_asset_free(a);
        return;
    }
    arena input = { 0 }, output = { 0 };
    pin_value args[5], operands[3], result;
    whole_array_op_arguments(kind, op, args, &input);
    for (int i = 0; i < operation->input_count; i++) {
        node *source;
        int pin;
        assert(graph_input_source(a->graphs[1], operation, i, &source, &pin));
        assert(source == a->graphs[1]->nodes[0]);
        operands[i] = args[pin - 1];
    }
    assert(!operation->desc->host_call->value(&output, operands, operation->input_count, &result));
    const pin_array *source = args[0].ptr;
    assert(source->count == 3);
    for (int i = 0; i < 3; i++) {
        assert(item_id(&source->items[i]) == i + 1);
    }
    arena_free(&input);
    behavior_asset_free(a);
    expected_default(&result, op);
    if (result.container >= PIN_CONTAINER_ARRAY) {
        const pin_array *array = result.ptr;
        assert(!strcmp(result.type_name, array->type_name));
        for (int i = 0; i < array->count; i++) {
            assert(!strcmp(array->items[i].type_name, array->type_name));
            if (kind == ARRAY_OP_STRUCTS) {
                const whole_managed *record = array->items[i].ptr;
                assert(!strcmp(record->label, record->nested.text));
                assert(record->nested.weight == record->count - .75f);
            }
        }
    }
    arena_free(&output);
}
static void parity(int kind, int op, bool waits, int scenario, int control, int budget, void *library)
{
    behavior_asset *a = whole_array_op_fixture(kind, op, waits);
    graph *child = a->graphs[1];
    assert(!graph_set_breakpoint(child, child->nodes[1], control == 3));
    behavior_function *f = body(a);
    void *code = f->code, *inspection = f->inspection_code, *dispatch = f->dispatch;
    test_flow_snapshot *expected = NULL;
    for (int mode = 0; mode < (library ? 4 : 3); mode++) {
        if (mode == 3) {
            char name[64];
            snprintf(name, sizeof name, "generated_array_op_%d_%d_%d", kind, op, waits);
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
        arena storage = { 0 };
        pin_value args[5];
        pin_array empty;
        whole_array_op_arguments(kind, op, args, &storage);
        scenario_inputs(scenario, args, &empty);
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_run *run = flow_run_start(a->graph, a->graph->nodes[0], (flow_inputs){ args, 5 }, &options, NULL);
        assert(run);
        arena_free(&storage);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int status = control == 1 ? flow_run_step(run) : flow_run_resume(run), at = 0;
        for (;;) {
            test_flow_snapshot snapshot = test_flow_snapshot_take(run, bi, status);
            if (!mode) {
                apush(expected, snapshot);
            } else {
                assert(run->whole_function_executed && at < acount(expected));
                if (snapshot.error.message && expected[at].error.message &&
                    strcmp(snapshot.error.message, expected[at].error.message)) {
                    fprintf(
                        stderr,
                        "array mismatch kind=%d op=%d waits=%d scenario=%d "
                        "control=%d budget=%d mode=%d: actual=%s expected=%s\n",
                        kind,
                        op,
                        waits,
                        scenario,
                        control,
                        budget,
                        mode,
                        snapshot.error.message,
                        expected[at].error.message
                    );
                }
                test_flow_snapshot_same(snapshot, expected[at]);
                test_flow_snapshot_free(&snapshot);
            }
            assert(++at < 64);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                if (!scenario && budget == 100) {
                    assert(state == FLOW_COMPLETED);
                    expected_default(&bi->variables[2], op);
                }
                if (scenario == 3 && op == ARRAY_OP_RESIZE && budget == 100) {
                    assert(state == FLOW_COMPLETED);
                    expected_growth(&bi->variables[2]);
                }
                if (op == ARRAY_OP_SET_ADD && budget == 100 &&
                    (scenario == 4 || scenario == 5 || scenario == 6)) {
                    const pin_array *set = bi->variables[2].ptr;
                    assert(state == FLOW_COMPLETED && bi->variables[2].container == PIN_CONTAINER_SET);
                    assert(set && set->count == (scenario == 6 ? 4 : 1));
                    assert(item_id(&set->items[set->count - 1]) == (scenario == 6 ? 4 : 2));
                }
                break;
            }
            if (state == FLOW_SUSPENDED) {
                pin_value replacement = bi->variables[0];
                replacement.ptr = NULL;
                assert(!behavior_instance_set_variable(bi, a->variables[0].id, &replacement));
                if (control == 2) {
                    assert(!flow_run_stop(run));
                    break;
                }
            }
            status = state == FLOW_SUSPENDED
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
static void set_returns(int kind, int op, void *library)
{
    for (int mode = 0; mode < (library ? 3 : 2); mode++) {
        behavior_asset *a = whole_array_op_fixture(kind, op, false);
        graph *g = a->graphs[1];
        behavior_program *program = behavior_program_publish(a);
        const behavior_function *f = behavior_program_function(
            behavior_program_graph(program, g),
            g->nodes[0]
        );
        assert(f);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        arena input = { 0 }, retained = { 0 };
        pin_value args[5], result[2] = { 0 };
        result[1].type = g->nodes[1]->input_pins[1].type;
        whole_array_op_arguments(kind, op, args, &input);
        behavior_function_context ctx = { .arguments = args,
                                          .argument_count = 5,
                                          .results = result,
                                          .budget = 100 };
        behavior_instance_bind_values(bi, &ctx);
        behavior_function_bind_storage(&ctx, &retained);
        behavior_function_activation activation = { 0 };
        if (behavior_function_resumable(f)) {
            behavior_function_activation_init(f, &activation);
            ctx.activation = &activation;
        }
        behavior_function_prepare_host(f, &ctx);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int status;
        if (mode == 2) {
            char name[64];
            snprintf(name, sizeof name, "generated_set_return_%d_%d", kind, op);
            int (*generated)(
                behavior_function_context *
            ) = (int (*)(behavior_function_context *))dlsym(library, name);
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
        arena_free(&input);
        expected_default(&result[1], op);
        if (kind == ARRAY_OP_STRUCTS && result[1].container == PIN_CONTAINER_SET) {
            const pin_array *set = result[1].ptr;
            const whole_managed *record = set->items[0].ptr;
            assert(!strcmp(record->label, op == ARRAY_OP_MAKE_SET ? "two" : "one"));
            assert(!strcmp(record->label, record->nested.text));
            assert(record->nested.weight == record->count - .75f);
        }
        arena_free(&retained);
    }
}
void test_array_ops(const char *path)
{
    test_owned_ticks();
    whole_array_op_smoke();
    void *library = path ? dlopen(path, RTLD_NOW | RTLD_LOCAL) : NULL;
    assert(!path || library);
    for (int kind = 0; kind < ARRAY_OP_KINDS; kind++) {
        for (int op = 0; op < ARRAY_OP_COUNT; op++) {
            if (op >= ARRAY_OP_MAKE_SET) {
                set_returns(kind, op, library);
            }
            helper_ownership(kind, op);
            for (int waits = 0; waits < 2; waits++) {
                for (int scenario = 0; scenario < 9; scenario++) {
                    parity(kind, op, waits, scenario, 0, 100, library);
                }
                parity(kind, op, waits, 0, 1, 100, library);
                parity(kind, op, waits, 0, 2, 100, library);
                parity(kind, op, waits, 0, 3, 100, library);
                parity(kind, op, waits, 0, 0, 3, library);
            }
        }
    }
    if (library) {
        assert(!dlclose(library));
    }
    test_note("PASS: whole array/set construction, mutation, membership, returns, strings/names/structs, errors, nested waits and debugger snapshots\n");
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_array_ops(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int kind = 0; kind < ARRAY_OP_KINDS; kind++) {
        for (int op = 0; op < ARRAY_OP_COUNT; op++) {
            behavior_asset *a = whole_array_op_fixture(kind, op, false);
            behavior_function *f = body(a);
            char name[64];
            snprintf(name, sizeof name, "generated_array_op_%d_%d_0", kind, op);
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
                arena input = { 0 }, storage = { 0 };
                pin_value args[5];
                whole_array_op_arguments(kind, op, args, &input);
                behavior_function_context ctx = { .arguments = args,
                                                  .argument_count = 5,
                                                  .budget = 100 };
                behavior_instance_bind_values(bi, &ctx);
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
                expected_default(&bi->variables[2], op);
                printf(
                    "%s array_op_%d_%d ns=%.3f checksum=%d\n",
                    (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C" }[mode],
                    kind,
                    op,
                    elapsed * 1e9 / count,
                    op + 1
                );
                behavior_function_release_host(&ctx);
                behavior_function_activation_free(&activation);
                arena_free(&storage);
                arena_free(&input);
                behavior_instance_destroy(bi);
            }
            behavior_asset_free(a);
        }
    }
    assert(!dlclose(library));
    return 0;
}
