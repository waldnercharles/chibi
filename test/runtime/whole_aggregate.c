#include "graph/runtime/flow_internal.h"
#include "graph/runtime/runtime.h"
#include "test/suite_internal.h"
#include "test/whole_aggregate_fixture.h"

#include <float.h>

static void same(pin_value a, pin_value b)
{
    assert(a.type == b.type && a.container == b.container);
    if (a.type == PIN_STRUCT) {
        assert((a.ptr != NULL) == (b.ptr != NULL));
        if (!a.ptr) {
            return;
        }
        const type_desc *type = registry_type_by_name(a.type_name);
        assert(type && !strcmp(a.type_name, b.type_name));
        for (int i = 0; i < type->field_count; i++) {
            pin_value x, y;
            pin_read_field(a.ptr, &type->fields[i], &x);
            pin_read_field(b.ptr, &type->fields[i], &y);
            same(x, y);
        }
        return;
    }
    switch (a.type) {
        case PIN_F32:
            assert(isnan(a.f) ? isnan(b.f) : a.f == b.f && signbit(a.f) == signbit(b.f));
            break;
        case PIN_F64:
            assert(isnan(a.d) ? isnan(b.d) : a.d == b.d && signbit(a.d) == signbit(b.d));
            break;
        case PIN_BOOL: assert(a.b == b.b); break;
        case PIN_U8: assert(a.u8 == b.u8); break;
        case PIN_I64: assert(a.i64 == b.i64); break;
        default: assert(false);
    }
}
static void parity(behavior_asset *a, const pin_value *inputs)
{
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, 0);
    for (int i = 1; i < acount(a->graphs); i++) {
        graph *g = a->graphs[i];
        assert(behavior_program_function(behavior_program_graph(a->program, g), g->nodes[0]));
    }
    for (int i = 0; i < acount(a->variables); i++) {
        assert(!behavior_instance_set_variable(bi, a->variables[i].id, &inputs[i]));
    }
    arena storage = { 0 };
    runtime_store *runtime = runtime_store_create(a->graph);
    node *call = a->graph->nodes[0];
    pin_value *expected = calloc((size_t)call->output_count, sizeof *expected);
    assert(expected);
    eval_status failure = { 0 };
    for (int mode = 0; mode < 3; mode++) {
        behavior_backend backend = (behavior_backend[]){ BEHAVIOR_ORACLE,
                                                         BEHAVIOR_BYTECODE,
                                                         behavior_default_backend() }[mode];
        assert(!behavior_instance_set_backend(bi, backend));
        runtime_store_reset_memo(runtime);
        eval_context ctx = { .instance = bi, .runtime = runtime };
        uint64_t oracle = eval_oracle_calls(),
                 allocations = runtime_store_creations();
        for (int i = 0; i < call->output_count; i++) {
            pin_value actual;
            int status = mode
                           ? behavior_program_output(
                                 behavior_program_graph(a->program, a->graph),
                                 call,
                                 i,
                                 &storage,
                                 &actual,
                                 &ctx,
                                 backend
                             )
                           : graph_eval_output(a->graph, call, i, &storage, &actual, &ctx);
            if (!mode) {
                failure = ctx.status;
                if (!status) {
                    assert(!pin_value_snapshot_to_arena(&storage, &expected[i], &actual));
                }
            } else {
                assert((status != 0) == (failure.code != 0));
                if (status) {
                    assert(
                        ctx.status.node == failure.node &&
                        ctx.status.pin == failure.pin
                    );
                } else {
                    same(expected[i], actual);
                }
            }
            if (status) {
                break;
            }
        }
        if (mode) {
            assert(oracle == eval_oracle_calls() && allocations == runtime_store_creations());
        }
    }
    free(expected);
    runtime_store_free(runtime);
    arena_free(&storage);
    behavior_instance_destroy(bi);
}
static behavior_asset *operation(const char *name, const pin_spec *inputs, int count, pin_spec result)
{
    behavior_asset *a = behavior_asset_new(name);
    graph *g = behavior_asset_add_function(a, "Operation", 1);
    node *entry = g->nodes[0], *ret = g->nodes[1],
         *op = graph_add_node(g, registry_node_by_name(name));
    assert(op && count == op->input_count);
    for (int i = 0; i < count; i++) {
        assert(graph_add_pin(g, entry, 1, &inputs[i]) == i);
        assert(!graph_set_pin_type(g, op, 0, i, inputs[i].type, inputs[i].type_name));
        assert(!graph_connect_data(g, entry, i, op, i));
        assert(behavior_asset_add_variable(
            a,
            inputs[i].name,
            inputs[i].type,
            inputs[i].type_name,
            PIN_CONTAINER_SINGLE,
            NULL,
            ""
        ));
    }
    assert(!graph_set_pin_type(g, op, 1, 0, result.type, result.type_name));
    /* Mixed arithmetic may produce a widened value; Return supplies the
     * consuming pin's coercion, just as a connected graph does. */
    if (result.type == PIN_F32) {
        result.type = PIN_F64;
        result.type_name = "double";
    }
    assert(graph_add_pin(g, ret, 0, &result) == 0);
    assert(!graph_connect_data(g, op, 0, ret, 0));
    node *call = behavior_asset_add_function_call_node(a, g);
    for (int i = 0; i < count; i++) {
        node *get = behavior_asset_add_get_node(a, a->variables[i].id);
        assert(!graph_connect_data(a->graph, get, 0, call, i));
    }
    return a;
}
static void aliased_identity()
{
    behavior_asset *a = behavior_asset_new("Aliased aggregate identity");
    graph *g = behavior_asset_add_function(a, "Identity", 1);
    pin_spec p = { .name = "Vector", .type = PIN_STRUCT, .type_name = "CF_V2" };
    assert(graph_add_pin(g, g->nodes[0], 1, &p) == 0);
    assert(graph_add_pin(g, g->nodes[1], 0, &p) == 0);
    assert(!graph_connect_data(g, g->nodes[0], 0, g->nodes[1], 0));
    node *call = behavior_asset_add_function_call_node(a, g);
    uint64_t id = behavior_asset_add_variable(a, "Transform", PIN_STRUCT, "GameTransform", PIN_CONTAINER_SINGLE, NULL, "")
                      ->id;
    node *get = behavior_asset_add_get_node(a, id);
    assert(!graph_connect_data(a->graph, get, 0, call, 0));
    CF_V2 v = { 3, 4 };
    pin_value input = { .type = PIN_STRUCT, .type_name = "GameTransform", .ptr = &v };
    parity(a, &input);
    behavior_asset_free(a);
}
static void aggregate_frames()
{
    behavior_asset *a = whole_aggregate_scopes();
    graph *g = a->graphs[4];
    whole_record input = { .tag = 251,
                           .position = { 3, 4 },
                           .weight = 2.75,
                           .active = true,
                           .stamp = INT64_MIN + 7 };
    pin_value args[] = { { .type = PIN_STRUCT, .type_name = "WholeRecord", .ptr = &input },
                         { .type = PIN_F32, .f = .25f } };
    for (unsigned budget = 1; budget <= 6; budget++) {
        for (int missing = 0; missing < 2; missing++) {
            args[0].ptr = missing ? NULL : &input;
            arena storage = { 0 };
            pin_value expected[2];
            int status = 0;
            unsigned long steps = 0;
            const node *error_node = NULL;
            int error_pin = -1;
            for (int mode = 0; mode < 3; mode++) {
                behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
                assert(!behavior_instance_set_backend(
                    bi,
                    (behavior_backend[]){ BEHAVIOR_ORACLE,
                                          BEHAVIOR_BYTECODE,
                                          behavior_default_backend() }[mode]
                ));
                behavior_instance_set_trace_enabled(bi, 0);
                flow_options options = { .instance = bi, .step_budget = budget };
                flow_error error = { 0 };
                uint64_t oracle = eval_oracle_calls();
                flow_run *run = flow_run_start(
                    g,
                    g->nodes[0],
                    (flow_inputs){ .values = args, .count = 2 },
                    &options,
                    &error
                );
                assert(run);
                int result = flow_run_resume(run);
                if (!mode) {
                    status = result;
                    steps = flow_run_steps(run);
                    error_node = flow_run_error(run)->node;
                    error_pin = flow_run_error(run)->pin;
                    if (!result) {
                        const pin_value *values = runtime_node_inputs(
                            runtime_store_node(run->runtime, g->nodes[1])
                        );
                        for (int i = 0; i < 2; i++) {
                            assert(
                                !pin_value_snapshot_to_arena(&storage, &expected[i], &values[i + 1])
                            );
                        }
                    }
                } else {
                    assert(run->whole_function_executed && !acount(run->children));
                    assert(result == status && flow_run_steps(run) == steps);
                    assert(
                        flow_run_error(run)->node == error_node &&
                        flow_run_error(run)->pin == error_pin
                    );
                    if (!result) {
                        const pin_value *values = runtime_node_inputs(
                            runtime_store_node(run->runtime, g->nodes[1])
                        );
                        for (int i = 0; i < 2; i++) {
                            same(expected[i], values[i + 1]);
                        }
                    }
                    assert(oracle == eval_oracle_calls());
                }
                flow_run_free(run);
                behavior_instance_destroy(bi);
            }
            arena_free(&storage);
        }
    }
    behavior_asset_free(a);
}
void test_compiled_aggregates()
{
    aggregate_frames();
    aliased_identity();
    whole_aggregate_smoke();
    behavior_asset *a = whole_aggregate_fixture();
    whole_record record = { .tag = 255,
                            .position = { -0., FLT_MAX },
                            .weight = -0.,
                            .active = true,
                            .stamp = INT64_MIN + 3 };
    pin_value args[] = { { .type = PIN_STRUCT, .type_name = "WholeRecord", .ptr = &record },
                         { .type = PIN_F32, .f = .125f } };
    parity(a, args);
    args[0].ptr = NULL;
    parity(a, args);
    behavior_asset_free(a);
    for (int project = 0; project < 2; project++) {
        a = whole_aggregate_identity(project);
        parity(a, args);
        args[0].ptr = &record;
        parity(a, args);
        args[0].ptr = NULL;
        behavior_asset_free(a);
    }
    double cases[] = { NAN,      INFINITY,     -INFINITY,     -0.,
                       0.,       FLT_TRUE_MIN, -FLT_TRUE_MIN, FLT_MAX,
                       -FLT_MAX, 1.e-40,       .375,          3. };
    pin_spec scalar[] = { { .name = "A", .type = PIN_F32, .type_name = "float" },
                          { .name = "B", .type = PIN_F64, .type_name = "double" } };
    const char *names[] = { "Add", "Subtract", "Multiply", "Divide",
                            "Min", "Max",      "Modulo" };
    for (int op = 0; op < 7; op++) {
        for (int order = 0; order < 2; order++) {
            scalar[0].type = order ? PIN_F64 : PIN_F32;
            scalar[0].type_name = order ? "double" : "float";
            scalar[1].type = order ? PIN_F32 : PIN_F64;
            scalar[1].type_name = order ? "float" : "double";
            a = operation(names[op], scalar, 2, scalar[0]);
            for (int x = 0; x < 12; x++) {
                for (int y = 0; y < 12; y++) {
                    pin_value values[2] = { { .type = scalar[0].type },
                                            { .type = scalar[1].type } };
                    if (order) {
                        values[0].d = cases[x];
                        values[1].f = (float)cases[y];
                    } else {
                        values[0].f = (float)cases[x];
                        values[1].d = cases[y];
                    }
                    parity(a, values);
                }
            }
            behavior_asset_free(a);
        }
    }
    pin_spec vectors[] = {
        { .name = "Origin", .type = PIN_STRUCT, .type_name = "CF_V2" },
        { .name = "Target", .type = PIN_STRUCT, .type_name = "CF_V2" }
    };
    for (int direction = 0; direction < 2; direction++) {
        a = operation(
            direction ? "Direction To" : "Normalize Vector 2",
            vectors,
            direction ? 2 : 1,
            vectors[0]
        );
        for (int x = 0; x < 12; x++) {
            for (int y = 0; y < 12; y++) {
                CF_V2 origin = { (float)cases[x], (float)cases[y] },
                      target = { 3, 4 };
                pin_value values[] = {
                    { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &origin },
                    { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &target }
                };
                parity(a, values);
            }
        }
        pin_value missing[] = { { .type = PIN_STRUCT, .type_name = "CF_V2" },
                                { .type = PIN_STRUCT, .type_name = "CF_V2" } };
        CF_V2 zero = { 0 };
        parity(a, missing);
        if (direction) {
            missing[0].ptr = &zero;
            parity(a, missing);
        }
        behavior_asset_free(a);
    }
    pin_spec angle = { .name = "Angle", .type = PIN_F64, .type_name = "double" };
    a = operation("Direction From Angle", &angle, 1, vectors[0]);
    for (int x = 0; x < 12; x++) {
        pin_value values[] = { { .type = PIN_F64, .type_name = "double", .d = cases[x] } };
        parity(a, values);
    }
    behavior_asset_free(a);
    test_note("PASS: aggregate ownership, nulls, projections, nested calls, mixed-width arithmetic and robust vector normalization match the oracle\n");
}
