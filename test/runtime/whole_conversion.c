#include "graph/compiler/behavior_scalar.h"
#include "graph/runtime/flow_internal.h"
#include "test/suite_internal.h"

#include <limits.h>

void test_compiled_conversions()
{
    const pin_type types[] = { PIN_BOOL, PIN_U8,  PIN_I32,
                               PIN_I64,  PIN_F32, PIN_F64 };
    const char *names[] = { "bool", "byte", "int", "int64", "float", "double" };
    const double values[] = { -INFINITY, -0x1p63, -257.5, -1.5,   -0.,      0.,
                              1.5,       255.5,   0x1p31, 0x1p63, INFINITY, NAN };
    for (int from = 0; from < 6; from++) {
        for (int to = 0; to < 6; to++) {
            const node_desc *conversion = conversion_for(types[from], types[to]);
            assert(conversion || types[from] != PIN_BOOL || types[to] == PIN_BOOL);
            if (!conversion) {
                continue;
            }
            behavior_asset *a = behavior_asset_new("Numeric edge body");
            graph *g = behavior_asset_add_function(a, "Convert", false);
            node *entry = g->nodes[0], *ret = g->nodes[1];
            pin_spec input = { .name = "Value",
                               .type = types[from],
                               .type_name = names[from] };
            pin_spec output = { .name = "Result",
                                .type = types[to],
                                .type_name = names[to] };
            assert(graph_add_pin(g, entry, 1, &input) == 1);
            assert(graph_add_pin(g, ret, 0, &output) == 1);
            assert(!graph_connect_data_conversion(g, entry, 1, ret, 1, conversion));
            assert(!graph_connect_exec(g, entry, 0, ret, 0, 0));
            for (int value = 0; value < 12; value++) {
                pin_value arg = { .type = types[from], .type_name = names[from] };
                switch (arg.type) {
                    case PIN_BOOL: arg.b = value & 1; break;
                    case PIN_U8:
                        arg.u8 = value == 11 ? UINT8_MAX : (uint8_t)value;
                        break;
                    case PIN_I32:
                        arg.i = value == 11 ? INT_MAX : INT_MIN + value;
                        break;
                    case PIN_I64:
                        arg.i64 = value == 11 ? INT64_MAX : INT64_MIN + value;
                        break;
                    case PIN_F32: arg.f = (float)values[value]; break;
                    case PIN_F64: arg.d = values[value]; break;
                    default: assert(false);
                }
                pin_value expected = { 0 };
                for (int mode = 0; mode < 3; mode++) {
                    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
                    behavior_instance_set_trace_enabled(bi, 0);
                    assert(!behavior_instance_set_backend(
                        bi,
                        (behavior_backend[]){ BEHAVIOR_ORACLE,
                                              BEHAVIOR_BYTECODE,
                                              behavior_default_backend() }[mode]
                    ));
                    flow_options options = { .instance = bi };
                    flow_error error = { 0 };
                    flow_run *run = flow_run_start(g, entry, (flow_inputs){ &arg, 1 }, &options, &error);
                    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
                    assert(run && !flow_run_resume(run));
                    pin_value result = *flow_run_input_value(run, ret, 1);
                    if (!mode) {
                        expected = result;
                        if (arg.type == PIN_BOOL) {
                            /* A bool reads as 1 or 0 in any number. */
                            double one = result.type == PIN_U8  ? result.u8
                                       : result.type == PIN_I32 ? result.i
                                       : result.type == PIN_I64 ? (double)result.i64
                                       : result.type == PIN_F32 ? result.f
                                                                : result.d;
                            assert(one == (arg.b ? 1 : 0));
                        }
                    } else {
                        assert(
                            run->whole_function_executed &&
                            result.type == expected.type
                        );
                        assert(!memcmp(
                            &result.d,
                            &expected.d,
                            behavior_scalar_size(result.type)
                        ));
                        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
                    }
                    flow_run_free(run);
                    behavior_instance_destroy(bi);
                }
            }
            behavior_asset_free(a);
        }
    }
    puts("PASS: numeric edge conversions, bool as 1 or 0, saturation and real widths in whole bodies");
}

static pin_value amount(pin_type type, const char *name, int value)
{
    pin_value result = { .type = type, .type_name = name };
    switch (type) {
        case PIN_BOOL: result.b = value & 1; break;
        case PIN_U8: result.u8 = (uint8_t)value; break;
        case PIN_I32: result.i = value; break;
        case PIN_I64: result.i64 = value; break;
        case PIN_F32: result.f = (float)value; break;
        default: result.d = value; break;
    }
    return result;
}

/* Select picks its option by an integer or a bool Index, over every scalar
 * width, the same in the oracle, bytecode and the default backend. */
void test_compiled_selects()
{
    const pin_type types[] = { PIN_BOOL, PIN_U8,  PIN_I32,
                               PIN_I64,  PIN_F32, PIN_F64 };
    const char *names[] = { "bool", "byte", "int", "int64", "float", "double" };
    for (int t = 0; t < 6; t++) {
        behavior_asset *a = behavior_asset_new("Select body");
        graph *g = behavior_asset_add_function(a, "Choose", false);
        node *entry = g->nodes[0], *ret = g->nodes[1];
        node *select = graph_add_node(g, registry_node_by_name("Select"));
        assert(select);
        pin_spec condition = { .name = "Pick", .type = PIN_BOOL, .type_name = "bool" };
        pin_spec no = { .name = "No", .type = types[t], .type_name = names[t] };
        pin_spec yes = { .name = "Yes", .type = types[t], .type_name = names[t] };
        pin_spec output = { .name = "Result", .type = types[t], .type_name = names[t] };
        assert(graph_add_pin(g, entry, 1, &condition) == 1);
        assert(graph_add_pin(g, entry, 1, &no) == 2);
        assert(graph_add_pin(g, entry, 1, &yes) == 3);
        assert(graph_add_pin(g, ret, 0, &output) == 1);
        const node_desc *flag = conversion_for(PIN_BOOL, PIN_I32);
        assert(!graph_connect_data_conversion(g, entry, 1, select, 0, flag));
        assert(!graph_connect_data(g, entry, 2, select, 1));
        assert(!graph_connect_data(g, entry, 3, select, 2));
        assert(!graph_connect_data(g, select, 0, ret, 1));
        assert(!graph_connect_exec(g, entry, 0, ret, 0, 0));
        for (int pick = 0; pick < 2; pick++) {
            pin_value arguments[] = { { .type = PIN_BOOL, .b = pick },
                                      amount(types[t], names[t], 3),
                                      amount(types[t], names[t], 6) };
            pin_value want = arguments[pick ? 2 : 1];
            for (int mode = 0; mode < 3; mode++) {
                behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
                behavior_instance_set_trace_enabled(bi, 0);
                assert(!behavior_instance_set_backend(
                    bi,
                    (behavior_backend[]){ BEHAVIOR_ORACLE,
                                          BEHAVIOR_BYTECODE,
                                          behavior_default_backend() }[mode]
                ));
                flow_options options = { .instance = bi };
                flow_error error = { 0 };
                flow_run *run = flow_run_start(g, entry, (flow_inputs){ arguments, 3 }, &options, &error);
                assert(run && !flow_run_resume(run));
                pin_value result = *flow_run_input_value(run, ret, 1);
                assert(result.type == want.type);
                assert(!memcmp(&result.d, &want.d, behavior_scalar_size(result.type)));
                flow_run_free(run);
                behavior_instance_destroy(bi);
            }
        }
        behavior_asset_free(a);
    }
    puts("PASS: Select picks by a bool Index in every scalar width and backend");
}
