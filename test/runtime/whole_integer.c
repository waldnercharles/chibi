#include "graph/nodes/compare_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/runtime.h"
#include "test/suite_internal.h"
#include "test/whole_integer_fixture.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static pin_value integer(pin_type type, int64_t n)
{
    return type == PIN_I32 ? (pin_value){ .type = type, .i = (int32_t)n }
                           : (pin_value){ .type = type, .i64 = n };
}
static void same(pin_value a, pin_value b)
{
    assert(a.type == b.type);
    switch (a.type) {
        case PIN_BOOL: assert(a.b == b.b); break;
        case PIN_I8: assert(a.i8 == b.i8); break;
        case PIN_U8: assert(a.u8 == b.u8); break;
        case PIN_I16: assert(a.i16 == b.i16); break;
        case PIN_U16: assert(a.u16 == b.u16); break;
        case PIN_I32: assert(a.i == b.i); break;
        case PIN_U32: assert(a.u == b.u); break;
        case PIN_I64: assert(a.i64 == b.i64); break;
        case PIN_U64: assert(a.u64 == b.u64); break;
        case PIN_ENUM: assert(a.e == b.e); break;
        case PIN_F64: assert(a.d == b.d); break;
        default: assert(false);
    }
}
static void nested(pin_type type, bool remainder_first)
{
    behavior_asset *a = whole_integer_fixture(type, remainder_first);
    graph *leaf = a->graphs[1];
    node *invalid = leaf->nodes[remainder_first ? 3 : 2];
    assert(invalid->desc == (remainder_first ? &NODE_Modulo : &NODE_Divide));
    int64_t lo = type == PIN_I32 ? INT32_MIN : INT64_MIN,
            hi = type == PIN_I32 ? INT32_MAX : INT64_MAX;
    int64_t cases[] = { lo, lo + 1, -17, -1, 0, 1, 3, 17, hi - 1, hi };
    for (int x = 0; x < 10; x++) {
        for (int y = 0; y < 10; y++) {
            pin_value args[] = { integer(type, cases[x]), integer(type, cases[y]) };
            pin_value expected[4];
            bool error_expected = !cases[y] || (cases[x] == lo && cases[y] == -1);
            for (int mode = 0; mode < 3; mode++) {
                behavior_backend backend = (behavior_backend[]){
                    BEHAVIOR_ORACLE,
                    BEHAVIOR_BYTECODE,
                    behavior_default_backend()
                }[mode];
                behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
                assert(!behavior_instance_set_backend(bi, backend));
                behavior_instance_set_trace_enabled(bi, 0);
                flow_error error = { 0 };
                uint64_t oracle = eval_oracle_calls();
                flow_run *run = behavior_instance_dispatch(
                    bi,
                    "Run",
                    (flow_inputs){ .values = args, .count = 2 },
                    &error
                );
                assert(run);
                assert(flow_run_status(run) == (error_expected ? FLOW_FAILED : FLOW_COMPLETED));
                assert(bi->variables[0].i == 1);
                if (error_expected) {
                    assert(flow_run_error(run)->code == FLOW_ERR_INVALID_GRAPH);
                    assert(
                        flow_run_error(run)->node == invalid &&
                        flow_run_error(run)->pin == 0
                    );
                    assert(flow_run_current_node(run) == invalid);
                }
                if (!mode) {
                    for (int i = 0; i < 4; i++) {
                        expected[i] = bi->variables[i];
                    }
                } else {
                    for (int i = 0; i < 4; i++) {
                        same(bi->variables[i], expected[i]);
                    }
                    assert(
                        run->whole_function_executed &&
                        !acount(run->children) && oracle == eval_oracle_calls()
                    );
                    if (error_expected) {
                        int count = 0;
                        const flow_call_frame *frames = flow_run_call_stack(run, &count);
                        assert(
                            count == 3 && frames[0].graph == a->graph &&
                            frames[1].graph == a->graphs[2]
                        );
                        assert(
                            frames[2].graph == leaf &&
                            frames[2].node_id == invalid->id
                        );
                    }
                }
                behavior_instance_destroy(bi);
            }
        }
    }
    behavior_asset_free(a);
}

static pin_value sample(pin_type type, int index)
{
    double real[] = { NAN,
                      INFINITY,
                      -INFINITY,
                      -0.,
                      .99,
                      -1.99,
                      254.99,
                      255.,
                      256.,
                      -0x1p31,
                      0x1p31 - 1.,
                      0x1p31,
                      -0x1p63,
                      nextafter(-0x1p63, 0),
                      nextafter(0x1p63, 0),
                      0x1p63 };
    int64_t lo = type == PIN_I32 ? INT32_MIN : INT64_MIN,
            hi = type == PIN_I32 ? INT32_MAX : INT64_MAX;
    int64_t values[] = { lo,         lo + 1,     -17,        -1,
                         0,          1,          3,          17,
                         hi - 1,     hi,         255,        256,
                         0x40000000, 0x7fffffff, 0x80000000, 0xffffffff };
    pin_value value = { .type = type };
    switch (type) {
        case PIN_I8: value.i8 = (int8_t)values[index]; break;
        case PIN_U8: value.u8 = (uint8_t)values[index]; break;
        case PIN_I16: value.i16 = (int16_t)values[index]; break;
        case PIN_U16: value.u16 = (uint16_t)values[index]; break;
        case PIN_I32: value.i = (int32_t)values[index]; break;
        case PIN_I64: value.i64 = values[index]; break;
        case PIN_U32: value.u = (uint32_t)values[index]; break;
        case PIN_U64: value.u64 = (uint64_t)values[index]; break;
        case PIN_ENUM: value.e = (int32_t)values[index]; break;
        case PIN_F32: value.f = (float)real[index]; break;
        case PIN_F64: value.d = real[index]; break;
        default: assert(false);
    }
    return value;
}

static void check_primitive(behavior_asset *a)
{
    pin_type from = a->variables[0].type;
    graph *f = a->graphs[1];
    node *op = f->nodes[2], *call = a->graph->nodes[0];
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, 0);
    assert(behavior_program_function(behavior_program_graph(a->program, f), f->nodes[0]));
    arena storage = { 0 };
    runtime_store *runtime = runtime_store_create(a->graph);
    int count = op->input_count == 3 ? 4096 : op->input_count == 2 ? 256 : 16;
    for (int k = 0; k < count; k++) {
        int index = k;
        for (int i = 0; i < op->input_count; i++) {
            bi->variables[i] = sample(from, index % 16);
            index /= 16;
        }
        pin_value expected = { 0 };
        int status = 0;
        for (int mode = 0; mode < 3; mode++) {
            behavior_backend backend = (behavior_backend[]){
                BEHAVIOR_ORACLE,
                BEHAVIOR_BYTECODE,
                behavior_default_backend()
            }[mode];
            assert(!behavior_instance_set_backend(bi, backend));
            runtime_store_reset_memo(runtime);
            eval_context ctx = { .instance = bi, .runtime = runtime };
            uint64_t oracle = eval_oracle_calls(),
                     allocations = runtime_store_creations();
            pin_value value;
            int result = mode == 0
                           ? graph_eval_output(a->graph, call, 0, &storage, &value, &ctx)
                           : behavior_program_output(
                                 behavior_program_graph(a->program, a->graph),
                                 call,
                                 0,
                                 &storage,
                                 &value,
                                 &ctx,
                                 backend
                             );
            if (!mode) {
                status = result;
                expected = value;
            } else {
                assert(result == status);
                if (!result) {
                    same(value, expected);
                }
                assert(oracle == eval_oracle_calls() && allocations == runtime_store_creations());
            }
            if (result) {
                assert(ctx.status.node == op && ctx.status.pin == 0);
            }
        }
    }
    runtime_store_free(runtime);
    arena_free(&storage);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

static void primitive(const node_desc *desc, pin_type from, pin_type to)
{
    check_primitive(whole_integer_operation(desc, from, to));
}

static void live_across_math()
{
    behavior_asset *a = whole_integer_operation(&NODE_Add, PIN_I64, PIN_I64);
    graph *f = a->graphs[1];
    node *entry = f->nodes[0], *ret = f->nodes[1], *sum = f->nodes[2],
         *real = graph_add_node(f, &NODE_To_Real),
         *sine = graph_add_node(f, registry_node_by_name("Sine")),
         *integer = graph_add_node(f, &NODE_To_Integer64),
         *result = graph_add_node(f, &NODE_Add);
    assert(graph_set_pin_type(f, real, 0, 0, PIN_I64, "int64") == 0);
    for (int i = 0; i < 2; i++) {
        assert(graph_set_pin_type(f, result, 0, i, PIN_I64, "int64") == 0);
    }
    assert(graph_set_pin_type(f, result, 1, 0, PIN_I64, "int64") == 0);
    assert(graph_remove_edge(f, sum, 0, ret, 0) == 0);
    assert(graph_connect_data(f, entry, 0, real, 0) == 0);
    assert(graph_connect_data(f, real, 0, sine, 0) == 0);
    assert(graph_connect_data(f, sine, 0, integer, 0) == 0);
    assert(graph_connect_data(f, sum, 0, result, 0) == 0);
    assert(graph_connect_data(f, integer, 0, result, 1) == 0);
    assert(graph_connect_data(f, result, 0, ret, 0) == 0);
    check_primitive(a);
}

static void loop_budgets()
{
    behavior_asset *a = whole_integer_loop();
    for (int budget = 1; budget <= 12; budget++) {
        int expected_status = 0, expected_count = 0;
        unsigned long expected_steps = 0;
        const node *failed = NULL;
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
            flow_run *run = flow_run_start(
                a->graph,
                a->graph->nodes[0],
                (flow_inputs){ 0 },
                &options,
                &error
            );
            assert(run);
            uint64_t oracle = eval_oracle_calls();
            int status = flow_run_resume(run);
            if (!mode) {
                expected_status = status;
                expected_count = bi->variables[0].i;
                expected_steps = flow_run_steps(run);
                failed = flow_run_error(run)->node;
            } else {
                assert(run->whole_function_executed && eval_oracle_calls() == oracle);
                assert(status == expected_status && bi->variables[0].i == expected_count);
                assert(
                    flow_run_steps(run) == expected_steps &&
                    flow_run_error(run)->node == failed
                );
            }
            if (!status) {
                assert(bi->variables[0].i == 4);
            }
            flow_run_free(run);
            behavior_instance_destroy(bi);
        }
    }
    behavior_asset_free(a);
}

void test_compiled_integers()
{
    nested(PIN_I32, false);
    nested(PIN_I32, true);
    nested(PIN_I64, false);
    nested(PIN_I64, true);
    loop_budgets();
    live_across_math();
    const node_desc *arithmetic[] = { &NODE_Add,      &NODE_Subtract,
                                      &NODE_Multiply, &NODE_Divide,
                                      &NODE_Modulo,   &NODE_Negate,
                                      &NODE_Abs,      &NODE_Min,
                                      &NODE_Max,      &NODE_Clamp };
    for (int i = 0; i < 10; i++) {
        primitive(arithmetic[i], PIN_I32, PIN_I32);
        primitive(arithmetic[i], PIN_I64, PIN_I64);
    }
    const node_desc *comparisons[] = {
        &NODE_Integer_Less,    &NODE_Integer_Less_Or_Equal,
        &NODE_Integer_Greater, &NODE_Integer_Greater_Or_Equal,
        &NODE_Integer_Equal,   &NODE_Integer_Not_Equal
    };
    for (int i = 0; i < 6; i++) {
        primitive(comparisons[i], PIN_I32, PIN_BOOL);
    }
    primitive(&NODE_Has_Any_Flags, PIN_U32, PIN_BOOL);
    const node_desc *casts[] = { &NODE_To_Byte, &NODE_To_Integer, &NODE_To_Integer64 };
    pin_type types[] = { PIN_U8, PIN_I32, PIN_I64 };
    for (int i = 0; i < 3; i++) {
        primitive(casts[i], PIN_F32, types[i]);
        primitive(casts[i], PIN_F64, types[i]);
        primitive(&NODE_To_Real, types[i], PIN_F64);
    }
    primitive(&NODE_To_Integer64, PIN_I32, PIN_I64);
    for (pin_type type = PIN_I8; type <= PIN_U64; type++) {
        primitive(registry_node_by_name("Integer"), type, type);
    }
    test_note("PASS: nested integer bodies, wrapping arithmetic, shared division/remainder and source scopes\n");
}
