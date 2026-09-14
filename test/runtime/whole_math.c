#include "graph/runtime/flow_internal.h"
#include "graph/runtime/runtime.h"
#include "test/suite_internal.h"
#include "test/whole_math_fixture.h"

static void same_real(double actual, double expected)
{
    assert(isnan(expected) ? isnan(actual) : actual == expected);
    assert(expected != 0 || signbit(actual) == signbit(expected));
}

static void primitive(const char *name, pin_type type)
{
    behavior_asset *a = behavior_asset_new(name);
    graph *f = behavior_asset_add_function(a, "Operation", 1);
    node *entry = graph_node_by_id(f, f->entry_id),
         *ret = graph_node_by_id(f, f->return_id);
    node *op = graph_add_node(f, registry_node_by_name(name));
    const char *type_name = type == PIN_F32 ? "float" : "double";
    for (int i = 0; i < op->input_count; i++) {
        pin_spec p = { .name = (const char *[]){ "A", "B", "C" }[i],
                       .kind = PIN_DATA,
                       .type = type,
                       .type_name = type_name };
        assert(graph_add_pin(f, entry, 1, &p) >= 0);
        assert(graph_set_pin_type(f, op, 0, i, type, type_name) == 0);
        assert(graph_connect_data(f, entry, i, op, i) == 0);
        assert(
            behavior_asset_add_variable(a, p.name, type, type_name, PIN_CONTAINER_SINGLE, NULL, "")
        );
    }
    assert(graph_set_pin_type(f, op, 1, 0, type, type_name) == 0);
    pin_spec output = { .name = "Value", .kind = PIN_DATA, .type = type, .type_name = type_name };
    assert(graph_add_pin(f, ret, 0, &output) >= 0);
    assert(graph_connect_data(f, op, 0, ret, 0) == 0);
    node *call = behavior_asset_add_function_call_node(a, f);
    for (int i = 0; i < op->input_count; i++) {
        node *get = behavior_asset_add_get_node(a, a->variables[i].id);
        assert(graph_connect_data(a->graph, get, 0, call, i) == 0);
    }
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, 0);
    assert(behavior_program_function(behavior_program_graph(a->program, f), entry));
    arena storage = { 0 };
    runtime_store *runtime = runtime_store_create(a->graph);
    double cases[] = { NAN, INFINITY, -INFINITY, -0.,   0.,
                       -1., 1.,       1.e-40,    1.e20, .375 };
    int count = op->input_count == 3 ? 1000 : op->input_count == 2 ? 100 : 10;
    for (int sample = 0; sample < count; sample++) {
        int index = sample;
        for (int i = 0; i < op->input_count; i++) {
            if (type == PIN_F32) {
                bi->variables[i].f = (float)cases[index % 10];
            } else {
                bi->variables[i].d = cases[index % 10];
            }
            index /= 10;
        }
        double expected = 0;
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
            assert(!result && value.type == type);
            double actual = type == PIN_F32 ? value.f : value.d;
            if (mode == 0) {
                expected = actual;
            } else {
                same_real(actual, expected);
                assert(oracle == eval_oracle_calls() && allocations == runtime_store_creations());
            }
        }
    }
    runtime_store_free(runtime);
    arena_free(&storage);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}

static void calls_preserve_state()
{
    behavior_asset *a = whole_math_fixture();
    node *event = a->graph->nodes[0];
    double cases[] = { NAN, INFINITY, -INFINITY, -0., 0., 1.e-40, .375 };
    for (unsigned budget = 1; budget <= 6; budget++) {
        for (int sample = 0; sample < 7; sample++) {
            pin_value args[8];
            for (int i = 0; i < 8; i++) {
                args[i] = event->output_pins[i + 1].default_value;
            }
            args[0].d = cases[sample];
            args[6].f = (float)cases[sample];
            args[7].b = sample % 2;
            pin_value expected[3];
            unsigned long steps = 0, error_steps = 0;
            int status = 0;
            const node *failed = NULL;
            for (int mode = 0; mode < 3; mode++) {
                behavior_backend backend = (behavior_backend[]){
                    BEHAVIOR_ORACLE,
                    BEHAVIOR_BYTECODE,
                    behavior_default_backend()
                }[mode];
                behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
                assert(!behavior_instance_set_backend(bi, backend));
                behavior_instance_set_trace_enabled(bi, 0);
                bi->variables[0].d = 2.75;
                flow_options options = { .instance = bi, .step_budget = budget };
                flow_error error = { 0 };
                uint64_t oracle = eval_oracle_calls();
                flow_run *run = flow_run_start(
                    a->graph,
                    event,
                    (flow_inputs){ .values = args, .count = 8 },
                    &options,
                    &error
                );
                assert(run);
                int actual = flow_run_resume(run);
                if (!mode) {
                    status = actual;
                    steps = flow_run_steps(run);
                    error_steps = flow_run_error(run)->steps;
                    failed = flow_run_error(run)->node;
                    for (int i = 0; i < 3; i++) {
                        expected[i] = bi->variables[i];
                    }
                } else {
                    assert(run->whole_function_executed && !acount(run->children));
                    assert(actual == status && flow_run_steps(run) == steps);
                    assert(
                        !actual || (flow_run_error(run)->node == failed &&
                                    flow_run_error(run)->steps == error_steps)
                    );
                    same_real(bi->variables[0].d, expected[0].d);
                    same_real(bi->variables[1].f, expected[1].f);
                    assert(bi->variables[2].b == expected[2].b && oracle == eval_oracle_calls());
                }
                flow_run_free(run);
                behavior_instance_destroy(bi);
            }
        }
    }
    behavior_asset_free(a);
}

void test_compiled_math()
{
    const char *names[] = { "Sine",        "Cosine",        "Tangent",
                            "Arc Sine",    "Arc Cosine",    "Arc Tangent",
                            "Exponential", "Logarithm",     "Logarithm 10",
                            "Power",       "Arc Tangent 2", "Clamp" };
    for (int i = 0; i < 12; i++) {
        primitive(names[i], PIN_F64);
    }
    primitive("Clamp", PIN_F32);
    calls_preserve_state();
    puts("PASS: whole math calls preserve scalar values, ABI registers, frame budgets and clamp edge cases");
}
