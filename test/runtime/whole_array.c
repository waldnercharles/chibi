#include "graph/runtime/flow_internal.h"
#include "test/suite_internal.h"
#include "test/whole_array_fixture.h"

static void same_record(const whole_record *a, const whole_record *b)
{
    assert((a != NULL) == (b != NULL));
    if (a) {
        assert(a->tag == b->tag && a->active == b->active && a->stamp == b->stamp);
        assert(!memcmp(&a->position, &b->position, sizeof a->position));
        assert(!memcmp(&a->weight, &b->weight, sizeof a->weight));
    }
}
static void parity(behavior_asset *a, int kind, int length, unsigned budget, int index)
{
    graph *g = a->graphs[3];
    whole_record records[4];
    pin_value items[4];
    for (int i = 0; i < 4; i++) {
        records[i] = (whole_record){ .tag = 200 + i,
                                     .position = { i + .5f, -i },
                                     .weight = i + .25,
                                     .stamp = INT64_MAX - i,
                                     .active = i & 1 };
        items[i] = kind == WHOLE_ARRAY_STRUCT
                     ? (pin_value){ .type = PIN_STRUCT,
                                    .type_name = "WholeRecord",
                                    .ptr = &records[i] }
                     : (pin_value){ .type = PIN_F64, .type_name = "double", .d = i + .25 };
    }
    if (index == 99 && kind == WHOLE_ARRAY_STRUCT) {
        items[3].ptr = NULL;
    }
    pin_array array = { .element_type = items[0].type,
                        .type_name = items[0].type_name,
                        .count = length < 0 ? 0 : length,
                        .capacity = 4,
                        .items = items };
    pin_value args[] = { { .type = items[0].type,
                           .type_name = items[0].type_name,
                           .container = PIN_CONTAINER_ARRAY,
                           .ptr = length < 0 ? NULL : &array },
                         { .type = PIN_I32, .i = index } };
    int status = 0, frame_count = 0;
    uintptr_t steps = 0;
    double sum = 0;
    pin_value outputs[7] = { 0 };
    whole_record record = { 0 };
    flow_error expected_error = { 0 };
    flow_call_frame frames[8];
    for (int mode = 0; mode < 3; mode++) {
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        assert(!behavior_instance_set_backend(
            bi,
            (behavior_backend[]){ BEHAVIOR_ORACLE,
                                  BEHAVIOR_BYTECODE,
                                  behavior_default_backend() }[mode]
        ));
        assert(!behavior_instance_set_variable(bi, a->variables[3].id, &args[0]));
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_error error = { 0 };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 2 }, &options, &error);
        assert(run);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = flow_run_resume(run), count = 0;
        const flow_call_frame *stack = flow_run_call_stack(run, &count);
        if (!mode) {
            status = result;
            sum = bi->variables[2].d;
            steps = flow_run_steps(run);
            expected_error = *flow_run_error(run);
            frame_count = count;
            assert(count <= 8);
            if (count) {
                memcpy(frames, stack, (size_t)count * sizeof *frames);
            }
            if (!result) {
                for (int i = 1; i < g->nodes[1]->input_count; i++) {
                    outputs[i] = *flow_run_input_value(run, g->nodes[1], i);
                }
                if (kind == WHOLE_ARRAY_STRUCT && outputs[3].ptr) {
                    record = *(whole_record *)outputs[3].ptr;
                    outputs[3].ptr = &record;
                }
            }
        } else {
            assert(run->whole_function_executed && !acount(run->children));
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            assert(
                result == status && steps == flow_run_steps(run) &&
                bi->variables[2].d == sum
            );
            if (result) {
                const flow_error *actual = flow_run_error(run);
                assert(
                    actual->node == expected_error.node &&
                    actual->pin == expected_error.pin &&
                    actual->steps == expected_error.steps && count == frame_count
                );
                for (int i = 0; i < count; i++) {
                    assert(
                        stack[i].graph_id == frames[i].graph_id &&
                        stack[i].node_id == frames[i].node_id
                    );
                }
                if (length < 0 && result != FLOW_ERR_STEP_BUDGET) {
                    assert(!strcmp(actual->message, expected_error.message));
                }
            } else {
                for (int i = 1; i < g->nodes[1]->input_count; i++) {
                    const pin_value *actual = flow_run_input_value(run, g->nodes[1], i);
                    if (kind == WHOLE_ARRAY_STRUCT && i == 3) {
                        same_record(actual->ptr, outputs[i].ptr);
                        assert(!actual->ptr || actual->ptr != records);
                    } else {
                        assert(
                            actual->type == outputs[i].type &&
                            actual->container == outputs[i].container
                        );
                        if (actual->type == PIN_F64) {
                            assert(
                                !memcmp(&actual->d, &outputs[i].d, sizeof actual->d)
                            );
                        } else if (actual->type == PIN_BOOL) {
                            assert(actual->b == outputs[i].b);
                        } else {
                            assert(actual->i == outputs[i].i);
                        }
                    }
                }
            }
        }
        flow_run_free(run);
        behavior_instance_destroy(bi);
    }
}
static void variable_write()
{
    behavior_asset *a = whole_array_fixture(WHOLE_ARRAY_RANGE);
    graph *g = a->graphs[3];
    node *entry = g->nodes[0], *loop = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i]->desc == &NODE_ForEach) {
            loop = g->nodes[i];
        }
    }
    assert(loop);
    node *set = graph_add_node(g, &NODE_SetVariable);
    assert(!graph_set_node_reference(g, set, a->variables[3].id, "Array"));
    assert(!graph_set_pin_type(g, set, 0, 1, PIN_F64, "double"));
    assert(!graph_set_pin_container(g, set, 0, 1, PIN_CONTAINER_ARRAY));
    assert(!graph_set_pin_type(g, set, 1, 1, PIN_F64, "double"));
    assert(!graph_set_pin_container(g, set, 1, 1, PIN_CONTAINER_ARRAY));
    assert(!graph_connect_data(g, entry, 1, set, 1));
    assert(!graph_remove_edge(g, entry, 0, loop, 0));
    assert(!graph_connect_exec(g, entry, 0, set, 0, 0));
    assert(!graph_connect_exec(g, set, 0, loop, 0, 0));
    behavior_function_diagnostic diagnostic = { 0 };
    behavior_function *function = behavior_function_lower(a, g, entry, &diagnostic);
    assert(function && !diagnostic.reason);
    behavior_function_free(function);
    for (int length = 0; length <= 4; length++) {
        parity(a, WHOLE_ARRAY_RANGE, length, 100, 2);
    }
    behavior_asset_free(a);
}
void test_compiled_arrays()
{
    for (int kind = 0; kind < WHOLE_ARRAY_COUNT; kind++) {
        behavior_asset *a = whole_array_fixture(kind);
        for (int length = -1; length <= 4; length++) {
            for (unsigned budget = 1; budget <= 32; budget++) {
                parity(a, kind, length, budget, 2);
            }
        }
        for (int index = -1; index <= 5; index++) {
            parity(a, kind, 4, 100, index);
        }
        parity(a, kind, 4, 100, 99);
        behavior_asset_free(a);
    }
    variable_write();
    whole_array_smoke();
    puts("PASS: array views, nulls, fields, bounds, nested loops/calls, Break, Return and budget/source parity");
}
