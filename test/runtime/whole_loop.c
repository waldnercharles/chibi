#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/suite_internal.h"
#include "test/whole_loop_fixture.h"

#include <limits.h>

static void guarded_budgets(behavior_function *f)
{
    assert(behavior_function_compile(f));
    const int ranges[][2] = { { 1, 4 },
                              { 4, 1 },
                              { 5, 5 },
                              { INT_MAX - 3, INT_MAX },
                              { INT_MIN + 3, INT_MIN },
                              { INT_MIN, INT_MAX },
                              { INT_MAX, INT_MIN } };
    for (int range = -1; range < (int)(sizeof ranges / sizeof *ranges); range++) {
        pin_value args[2] = { { .type = PIN_I32 }, { .type = PIN_I32 } };
        if (range >= 0) {
            args[0].i = ranges[range][0];
            args[1].i = ranges[range][1];
        }
        for (uintptr_t budget = 1; budget <= 40; budget++) {
            uintptr_t initials[] = { 0, budget - 1, budget, budget + 1, UINTPTR_MAX };
            for (int initial = 0; initial < 5; initial++) {
                behavior_function_context expected = { 0 };
                pin_value expected_variables[2] = { 0 }, expected_results[2] = { 0 };
                int status = 0;
                for (int native = 0; native < 2; native++) {
                    pin_value variables[2] = { { .type = PIN_I32 },
                                               { .type = PIN_I32, .i = 1 } };
                    pin_value results[2] = { { .type = PIN_BOOL },
                                             { .type = PIN_I32, .type_name = "int" } };
                    behavior_function_context ctx = {
                        .arguments = range < 0 ? NULL : args,
                        .argument_count = range < 0 ? 0 : 2,
                        .variables = variables,
                        .results = results,
                        .steps = initials[initial],
                        .budget = budget
                    };
                    int actual = behavior_function_run(f, &ctx, native);
                    if (!native) {
                        status = actual;
                        expected = ctx;
                        memcpy(expected_variables, variables, sizeof variables);
                        memcpy(expected_results, results, sizeof results);
                        continue;
                    }
                    assert(actual == status && ctx.steps == expected.steps);
                    assert(
                        variables[0].i == expected_variables[0].i &&
                        variables[1].i == expected_variables[1].i
                    );
                    if (actual) {
                        assert(
                            ctx.node_id == expected.node_id &&
                            ctx.error_steps == expected.error_steps
                        );
                        assert(
                            ctx.error_frame == expected.error_frame &&
                            ctx.error_source == expected.error_source
                        );
                        assert(
                            ctx.error_stack == expected.error_stack &&
                            ctx.error_pin == expected.error_pin
                        );
                    }
                    if (!actual) {
                        assert(results[1].i == expected_results[1].i);
                        assert(ctx.returned_node_id == expected.returned_node_id);
                    }
                }
            }
        }
    }
}

/* Public callers may supply overlapping pin buffers. Deferred successful
 * publication must use the checked path when results alias a variable. */
static void guarded_aliases(behavior_function *f)
{
    for (int offset = -1; offset <= 1; offset++) {
        for (int argument_alias = 0; argument_alias < 2; argument_alias++) {
            for (uintptr_t budget = 1; budget <= 24; budget++) {
                pin_value expected[4] = { 0 };
                uintptr_t steps = 0;
                int status = 0;
                for (int native = 0; native < 2; native++) {
                    pin_value shared[4] = { { .type = PIN_I32, .i = 8 },
                                            { .type = PIN_I32, .i = 1 },
                                            { .type = PIN_I32, .i = 4 },
                                            { .type = PIN_I32, .i = 9 } };
                    pin_value args[2] = { { .type = PIN_I32, .i = 1 },
                                          { .type = PIN_I32, .i = 4 } };
                    behavior_function_context ctx = {
                        .arguments = argument_alias ? shared + 1 : args,
                        .argument_count = 2,
                        .variables = shared + 1,
                        .results = shared + 1 + offset,
                        .steps = 3,
                        .budget = budget
                    };
                    int actual = behavior_function_run(f, &ctx, native);
                    if (!native) {
                        memcpy(expected, shared, sizeof shared);
                        steps = ctx.steps;
                        status = actual;
                    } else {
                        assert(actual == status && ctx.steps == steps);
                        for (int i = 0; i < 4; i++) {
                            assert(shared[i].i == expected[i].i);
                            assert(shared[i].type == expected[i].type);
                            assert(shared[i].type_name == expected[i].type_name);
                        }
                    }
                }
            }
        }
    }
}

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
        if (kind == WHOLE_LOOP_RANGE || kind == WHOLE_LOOP_EMPTY ||
            kind == WHOLE_LOOP_SNAPSHOT || kind == WHOLE_LOOP_NESTED ||
            kind == WHOLE_LOOP_BREAK || kind == WHOLE_LOOP_RETURN) {
            behavior_function_diagnostic diagnostic;
            behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
            assert(f);
            behavior_loop_budget proof;
            bool proven = behavior_function_loop_budget(f, &proof);
            assert(proven == (kind == WHOLE_LOOP_RANGE || kind == WHOLE_LOOP_EMPTY));
            if (proven) {
                assert(proof.entry_steps && proof.iteration_steps);
                guarded_budgets(f);
                guarded_aliases(f);
            }
            behavior_function_free(f);
        }
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
    puts("PASS: whole loops, Break, snapshots, returns, budgets and call stacks across oracle, SLJIT and bytecode");
}
