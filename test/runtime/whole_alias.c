#include "graph/runtime/flow_internal.h"
#include "test/suite_internal.h"
#include "test/whole_alias_fixture.h"

static void parity(behavior_asset *a, bool missing, unsigned budget)
{
    float vector[] = { -0.f, NAN };
    graph *g = a->graphs[2];
    pin_value input = { .type = PIN_STRUCT,
                        .ptr = missing ? NULL : vector,
                        .type_name = g->nodes[0]->output_pins[1].type_name };
    int status = 0, frame_count = 0;
    unsigned long steps = 0;
    flow_error expected = { 0 };
    flow_call_frame frames[4];
    for (int mode = 0; mode < 5; mode++) {
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, mode >= 3);
        assert(!behavior_instance_set_backend(
            bi,
            (behavior_backend[]){ BEHAVIOR_ORACLE,
                                  BEHAVIOR_BYTECODE,
                                  behavior_default_backend(),
                                  BEHAVIOR_BYTECODE,
                                  behavior_default_backend() }[mode]
        ));
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_error error = { 0 };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &input, 1 }, &options, &error);
        assert(run);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = flow_run_resume(run), count = 0;
        const flow_call_frame *stack = flow_run_call_stack(run, &count);
        if (!mode) {
            status = result;
            steps = flow_run_steps(run);
            expected = *flow_run_error(run);
            frame_count = count;
            assert(count <= 4);
            if (count) {
                memcpy(frames, stack, (size_t)count * sizeof *frames);
            }
        } else {
            assert(
                run->whole_function_executed == (mode < 3) &&
                result == status && steps == flow_run_steps(run)
            );
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            if (result) {
                const flow_error *actual = flow_run_error(run);
                assert(actual->node == expected.node && actual->pin == expected.pin);
                assert(count == frame_count && actual->steps == expected.steps);
                for (int i = 0; i < count; i++) {
                    assert(
                        stack[i].node_id == frames[i].node_id &&
                        stack[i].graph_id == frames[i].graph_id
                    );
                }
                if (result != FLOW_ERR_STEP_BUDGET) {
                    assert(!strcmp(actual->message, expected.message));
                }
            }
        }
        if (!result) {
            const pin_value *out = flow_run_input_value(run, g->nodes[1], 1);
            assert(out->ptr != vector && !memcmp(out->ptr, vector, sizeof vector));
            assert(!strcmp(out->type_name, g->nodes[1]->input_pins[1].type_name));
        }
        flow_run_free(run);
        behavior_instance_destroy(bi);
    }
}
void test_compiled_aliases()
{
    for (int reverse = 0; reverse < 2; reverse++) {
        for (int edge = 0; edge < 2; edge++) {
            for (int pure = 0; pure < 2; pure++) {
                behavior_asset *a = whole_alias_fixture(
                    registry_node_by_name(reverse ? "game_transform_from_cf" : "game_transform_to_cf"),
                    edge,
                    pure
                );
                for (unsigned budget = 1; budget < 9; budget++) {
                    parity(a, false, budget);
                    parity(a, true, budget);
                }
                /* Compatible shapes alone never promise identity. */
                node_desc changed = *registry_node_by_name(
                    reverse ? "game_transform_from_cf" : "game_transform_to_cf"
                );
                changed.jit_kind = NODE_JIT_NONE;
                graph *g = a->graphs[1];
                if (edge) {
                    g->edges[0].conversion = &changed;
                } else {
                    g->nodes[2]->desc = &changed;
                }
                behavior_function_diagnostic diagnostic = { 0 };
                behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
                assert(!f && diagnostic.reason);
                behavior_asset_free(a);
            }
        }
    }
    whole_alias_smoke();
    puts("PASS: explicit and edge alias conversions, nested calls, null diagnostics and budgets across all backends");
}
