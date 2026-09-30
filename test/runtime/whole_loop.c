#include "graph/runtime/flow_internal.h"
#include "test/suite_internal.h"
#include "test/whole_loop_fixture.h"

#include <limits.h>

void test_compiled_loops()
{
    const int ranges[][2] = { { 1, 4 },
                              { 4, 1 },
                              { 5, 5 },
                              { -2, 2 },
                              { INT_MAX - 1, INT_MAX },
                              { INT_MIN + 1, INT_MIN },
                              { INT_MAX, INT_MIN } };
    for (int kind = 0; kind < WHOLE_LOOP_COUNT; kind++) {
        behavior_asset *a = whole_loop_fixture(kind);
        graph *g = a->graphs[1];
        for (int range = 0; range < (int)(sizeof ranges / sizeof *ranges); range++) {
            for (int budget = 1; budget <= 65; budget++) {
                int status = 0, expected_count = 0, expected_limit = 0, returned = 0;
                flow_error expected_error = { 0 };
                flow_call_frame frames[4] = { 0 };
                int frame_count = 0;
                unsigned long steps = 0;
                for (int mode = 0; mode < 3; mode++) {
                    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
                    behavior_instance_set_trace_enabled(bi, 0);
                    assert(!behavior_instance_set_backend(
                        bi,
                        (behavior_backend[]){ BEHAVIOR_ORACLE,
                                              BEHAVIOR_BYTECODE,
                                              behavior_default_backend() }[mode]
                    ));
                    pin_value args[] = { { .type = PIN_I32, .i = ranges[range][0] },
                                         { .type = PIN_I32, .i = ranges[range][1] } };
                    flow_options options = { .instance = bi,
                                             .step_budget = (unsigned)budget };
                    flow_error error = { 0 };
                    flow_run *run = flow_run_start(
                        g,
                        g->nodes[0],
                        (flow_inputs){ args, 2 },
                        &options,
                        &error
                    );
                    assert(run);
                    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
                    int result = flow_run_resume(run);
                    int count = 0;
                    const flow_call_frame *stack = flow_run_call_stack(run, &count);
                    if (!mode) {
                        status = result;
                        expected_count = bi->variables[0].i;
                        expected_limit = bi->variables[1].i;
                        steps = flow_run_steps(run);
                        expected_error = *flow_run_error(run);
                        frame_count = count;
                        assert(count <= 4);
                        if (count) {
                            memcpy(frames, stack, (size_t)count * sizeof *frames);
                        }
                        if (!result) {
                            returned = flow_run_input_value(run, g->nodes[1], 1)->i;
                        }
                    } else {
                        assert(run->whole_function_executed && !acount(run->children));
                        assert(result == status && steps == flow_run_steps(run));
                        assert(
                            bi->variables[0].i == expected_count &&
                            bi->variables[1].i == expected_limit
                        );
                        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
                        const flow_error *actual = flow_run_error(run);
                        assert(
                            actual->node == expected_error.node &&
                            actual->pin == expected_error.pin
                        );
                        if (result) {
                            assert(actual->steps == expected_error.steps && count == frame_count);
                            for (int i = 0; i < count; i++) {
                                assert(
                                    stack[i].node_id == frames[i].node_id &&
                                    stack[i].graph_id == frames[i].graph_id
                                );
                            }
                            if (result != FLOW_ERR_STEP_BUDGET) {
                                assert(!strcmp(actual->message, expected_error.message));
                            }
                        } else {
                            assert(flow_run_input_value(run, g->nodes[1], 1)->i == returned);
                        }
                    }
                    flow_run_free(run);
                    behavior_instance_destroy(bi);
                }
            }
        }
        behavior_asset_free(a);
    }
    whole_loop_smoke();
    test_note("PASS: whole loops, Break, snapshots, returns, budgets and call stacks across oracle, SLJIT and bytecode\n");
}
