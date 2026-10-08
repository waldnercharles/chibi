#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/behavior_function.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_array_fixture.h"
#include "test/whole_delay_fixture.h"
#include "test/whole_loop_fixture.h"

static void parity(behavior_asset *a, graph *g, flow_inputs inputs, unsigned budget, int step)
{
    test_flow_snapshot *expected = NULL;
    for (int mode = 0; mode < 3; mode++) {
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, step >= 5);
        assert(!behavior_instance_set_backend(
            bi,
            (behavior_backend[]){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT }[mode]
        ));
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_run *run = flow_run_start(g, g->nodes[0], inputs, &options, NULL);
        assert(run);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = step == 6 ? flow_run_step(run) : flow_run_resume(run),
            at = 0, waits = 0;
        do {
            if (mode) {
                assert(run->whole_function_executed && !acount(run->children));
            }
            test_flow_snapshot s = test_flow_snapshot_take(run, bi, result);
            if (!mode) {
                apush(expected, s);
            }
            if (mode) {
                assert(at < acount(expected));
                test_flow_snapshot_same(s, expected[at]);
                test_flow_snapshot_free(&s);
            }
            assert(++at < 300);
            flow_state state = flow_run_status(run);
            if (state == FLOW_COMPLETED || state == FLOW_FAILED) {
                break;
            }
            if (state == FLOW_SUSPENDED) {
                assert(flow_run_tick_at(run, flow_run_wake_time(run) - .01) == FLOW_SUSPENDED_RESULT);
                if (step && (step != 3 || !waits)) {
                    assert(!flow_run_pause(run));
                }
                if (step == 4) {
                    behavior_instance_set_trace_enabled(bi, true);
                }
                if (step == 7) {
                    behavior_instance_set_trace_enabled(bi, waits % 3 == 2);
                }
                waits++;
                result = flow_run_tick_at(run, flow_run_wake_time(run));
            } else {
                assert(state == FLOW_PAUSED);
                result = (step == 2 || step == 3) ? flow_run_step_over(run)
                                                  : flow_run_step(run);
            }
        } while (true);
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
}
static void array_watches()
{
    int kinds[] = { WHOLE_ARRAY_RANGE,  WHOLE_ARRAY_STRUCT, WHOLE_ARRAY_CALL,
                    WHOLE_ARRAY_NESTED, WHOLE_ARRAY_BREAK,  WHOLE_ARRAY_CALL };
    for (int k = 0; k < (int)(sizeof kinds / sizeof *kinds); k++) {
        behavior_asset *a = whole_array_fixture(kinds[k]);
        whole_delay_add(a);
        if (k == 5) {
            graph *child = a->graphs[4];
            node *ret = graph_node_by_id(child, child->return_id);
            bool removed = false;
            for (int i = 0; i < acount(child->nodes); i++) {
                node *n = child->nodes[i];
                if (n->desc == &NODE_ForEach) {
                    assert(!graph_remove_edge(child, n, 3, ret, 0));
                    removed = true;
                }
            }
            assert(removed);
        }
        whole_record records[] = {
            { .tag = 255, .position = { .5f, -.25f }, .weight = 1.5, .active = true, .stamp = INT64_MAX },
            { .tag = 42, .position = { -0.f, 3.25f }, .weight = -2.5, .stamp = INT64_MIN }
        };
        pin_value items[2];
        for (int i = 0; i < 2; i++) {
            items[i] = kinds[k] == WHOLE_ARRAY_STRUCT
                         ? (pin_value){ .type = PIN_STRUCT,
                                        .type_name = "WholeRecord",
                                        .ptr = &records[i] }
                         : (pin_value){ .type = PIN_F64,
                                        .type_name = "double",
                                        .d = records[i].weight };
        }
        pin_array array = { .element_type = items[0].type,
                            .type_name = items[0].type_name,
                            .items = items,
                            .capacity = 2 };
        pin_value args[] = { { .type = items[0].type,
                               .type_name = items[0].type_name,
                               .container = PIN_CONTAINER_ARRAY,
                               .ptr = &array },
                             { .type = PIN_I32, .i = 1 } };
        for (int length = 0; length <= 2; length++) {
            array.count = length;
            for (int budget = 1; budget <= 20; budget++) {
                for (int step = 5; step < 8; step++) {
                    parity(a, a->graphs[3], (flow_inputs){ args, 2 }, budget, step);
                }
            }
        }
        behavior_asset_free(a);
    }
}
static void lifecycle(behavior_backend backend);
static void durations();
static void breakpoints();
void test_compiled_delays()
{
    int kinds[] = { WHOLE_LOOP_RANGE,       WHOLE_LOOP_BREAK,
                    WHOLE_LOOP_NESTED,      WHOLE_LOOP_WHILE,
                    WHOLE_LOOP_NESTED_CALL, WHOLE_LOOP_SNAPSHOT };
    for (int kind = 0; kind < (int)(sizeof kinds / sizeof *kinds); kind++) {
        behavior_asset *a = whole_delay_fixture(kinds[kind]);
        pin_value args[] = { { .type = PIN_I32, .i = 1 }, { .type = PIN_I32, .i = 3 } };
        for (unsigned budget = 1; budget <= 45; budget++) {
            for (int step = 0; step < 8; step++) {
                parity(a, a->graphs[1], (flow_inputs){ args, 2 }, budget, step);
            }
        }
        behavior_asset_free(a);
    }
    array_watches();
    lifecycle(BEHAVIOR_SLJIT);
    lifecycle(BEHAVIOR_BYTECODE);
    lifecycle(BEHAVIOR_ORACLE);
    durations();
    breakpoints();
    test_delay_entries(NULL);
    whole_delay_smoke();
    puts("PASS: whole Delay loops, nested calls, budgets, stepping, scalar/array/struct watches and traversal parity");
}

static behavior_asset *event_fixture(double duration)
{
    behavior_asset *a = behavior_asset_new("Whole delay lifecycle");
    behavior_variable *v = behavior_asset_add_variable(a, "Count", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "");
    graph *g = a->graph;
    node *event = graph_add_node(g, &NODE_Event),
         *delay = graph_add_node(g, &NODE_Delay);
    node *set = behavior_asset_add_set_node(a, v->id);
    assert(!graph_set_event_name(g, event, "Run"));
    assert(!graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = duration }));
    assert(!graph_set_pin_default(g, set, 0, 1, &(pin_value){ .type = PIN_I32, .i = 7 }));
    assert(!graph_connect_exec(g, event, 0, delay, 0, 0));
    assert(!graph_connect_exec(g, delay, 0, set, 0, 0));
    return a;
}
static void lifecycle(behavior_backend backend)
{
    behavior_asset *a = event_fixture(.5), *next = event_fixture(1.);
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(bi, backend));
    behavior_instance_set_trace_enabled(bi, false);
    flow_error error;
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int iteration = 0; iteration < 4; iteration++) {
        behavior_instance_set_trace_enabled(bi, iteration % 2 == 0);
        flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
        assert(run && flow_run_status(run) == FLOW_SUSPENDED);
        assert(backend == BEHAVIOR_ORACLE || (run->whole_function_executed && run->compiled));
        assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_BUSY);
        if (iteration % 2 == 0) {
            assert(!behavior_instance_stop_activation(bi, run));
            assert(!behavior_instance_tick(bi, .5 + iteration));
            assert(bi->execution.variables[0].i == 0);
            assert(!behavior_instance_release_activation(bi, run));
        } else {
            assert(!behavior_instance_tick(bi, flow_run_wake_time(run)));
            assert(
                flow_run_status(run) == FLOW_COMPLETED &&
                bi->execution.variables[0].i == 7
            );
            assert(!behavior_instance_release_activation(bi, run));
            assert(!behavior_instance_set_variable(
                bi,
                a->variables[0].id,
                &(pin_value){ .type = PIN_I32, .i = 0 }
            ));
        }
    }
    assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY);
    behavior_instance_update(bi, next);
    behavior_asset_free(a);
    flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_SUSPENDED);
    assert(flow_run_wake_time(run) == bi->local_clock.current_time + 1.);
    behavior_instance_destroy(bi);
    behavior_asset_free(next);
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
}
static void breakpoints()
{
    behavior_asset *a = event_fixture(.5);
    graph *g = a->graph;
    node *set = g->nodes[2];
    assert(!graph_set_breakpoint(g, set, true));
    for (int mode = 0; mode < 3; mode++) {
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(
            bi,
            (behavior_backend[]){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT }[mode]
        ));
        behavior_instance_set_trace_enabled(bi, false);
        flow_options options = { .instance = bi,
                                 .breakpoints_override = true,
                                 .breakpoints_enabled = true };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ 0 }, &options, NULL);
        assert(run && flow_run_resume(run) == FLOW_SUSPENDED_RESULT);
        assert(!mode || run->whole_function_executed);
        assert(flow_run_tick_at(run, .5) == FLOW_PAUSED_RESULT);
        assert(flow_run_current_node(run) == set && bi->execution.variables[0].i == 0);
        assert(flow_run_steps(run) == 2);
        assert(
            flow_run_step(run) == FLOW_PAUSED_RESULT &&
            bi->execution.variables[0].i == 7
        );
        assert(!flow_run_current_node(run));
        assert(!flow_run_resume(run) && flow_run_status(run) == FLOW_COMPLETED);
        flow_run_free(run);
        behavior_instance_destroy(bi);
    }
    behavior_asset_free(a);
}
static void durations()
{
    const double durations[] = { 0., -.5, .5, INFINITY, NAN };
    for (int d = 0; d < 5; d++) {
        behavior_asset *a = event_fixture(durations[d]);
        for (int mode = 0; mode < 3; mode++) {
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            assert(!behavior_instance_set_backend(
                bi,
                (behavior_backend[]){ BEHAVIOR_ORACLE, BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT }[mode]
            ));
            behavior_instance_set_trace_enabled(bi, false);
            flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, NULL);
            assert(run);
            if (d < 2) {
                assert(
                    flow_run_status(run) == FLOW_COMPLETED &&
                    bi->execution.variables[0].i == 7
                );
                assert(flow_run_wake_time(run) == 0.);
            } else {
                assert(
                    flow_run_status(run) == FLOW_SUSPENDED &&
                    bi->execution.variables[0].i == 0
                );
                double wake = flow_run_wake_time(run);
                assert(d == 4 ? isnan(wake) : wake == durations[d]);
                assert(!flow_run_tick_at(run, d == 3 ? INFINITY : 1.));
                assert(
                    flow_run_status(run) == FLOW_COMPLETED &&
                    bi->execution.variables[0].i == 7
                );
            }
            behavior_instance_destroy(bi);
        }
        behavior_asset_free(a);
    }
}
