#include "graph/runtime/flow_internal.h"
#include "test/suite_internal.h"
#include "test/whole_store_fixture.h"

static pin_value snapshot(arena *storage, pin_value v)
{
    pin_value out;
    assert(!flow_copy_input(storage, &out, &v));
    if (v.type_name) {
        char *name = arena_alloc(storage, strlen(v.type_name) + 1);
        strcpy(name, v.type_name);
        out.type_name = name;
    }
    return out;
}
static void same(pin_value a, pin_value b)
{
    assert(a.type == b.type && a.container == b.container);
    assert(!strcmp(a.type_name ? a.type_name : "", b.type_name ? b.type_name : ""));
    assert((a.ptr != NULL) == (b.ptr != NULL));
    if (!a.ptr) {
        return;
    }
    const whole_record *x = a.ptr, *y = b.ptr;
    assert(x->tag == y->tag && x->stamp == y->stamp && x->active == y->active);
    assert(!memcmp(&x->position, &y->position, sizeof x->position));
    assert(!memcmp(&x->weight, &y->weight, sizeof x->weight));
}
static void parity(behavior_asset *a, bool missing, unsigned budget)
{
    whole_record input = { .tag = 251,
                           .position = { -0.f, 4 },
                           .weight = -0.,
                           .active = true,
                           .stamp = INT64_MIN + 7 };
    pin_value arg = { .type = PIN_STRUCT,
                      .type_name = "WholeRecord",
                      .ptr = missing ? NULL : &input };
    graph *g = a->graphs[4];
    arena storage = { 0 };
    pin_value expected_value = { 0 }, expected_result = { 0 };
    flow_error expected_error = { 0 };
    flow_call_frame frames[8];
    int status = 0, count = 0;
    unsigned long steps = 0;
    for (int mode = 0; mode < 3; mode++) {
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        assert(!behavior_instance_set_backend(
            bi,
            (behavior_backend[]){ BEHAVIOR_ORACLE,
                                  BEHAVIOR_BYTECODE,
                                  behavior_default_backend() }[mode]
        ));
        void *backing = bi->variable_storage[0].data;
        flow_options options = { .instance = bi, .step_budget = budget };
        flow_error error = { 0 };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &arg, 1 }, &options, &error);
        assert(run);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = flow_run_resume(run), actual_count = 0;
        const flow_call_frame *stack = flow_run_call_stack(run, &actual_count);
        if (!mode) {
            status = result;
            steps = flow_run_steps(run);
            expected_error = *flow_run_error(run);
            expected_value = snapshot(&storage, bi->variables[0]);
            count = actual_count;
            assert(count <= 8);
            if (count) {
                memcpy(frames, stack, (size_t)count * sizeof *frames);
            }
            if (!result) {
                expected_result = snapshot(&storage, *flow_run_input_value(run, g->nodes[1], 1));
            }
        } else {
            assert(
                run->whole_function_executed && result == status &&
                flow_run_steps(run) == steps
            );
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            same(bi->variables[0], expected_value);
            if (!result) {
                same(*flow_run_input_value(run, g->nodes[1], 1), expected_result);
            } else {
                const flow_error *actual = flow_run_error(run);
                assert(
                    actual->node == expected_error.node &&
                    actual->pin == expected_error.pin &&
                    actual->steps == expected_error.steps
                );
                assert(actual_count == count);
                for (int i = 0; i < count; i++) {
                    assert(
                        stack[i].graph_id == frames[i].graph_id &&
                        stack[i].node_id == frames[i].node_id
                    );
                }
            }
        }
        assert(
            bi->variable_storage[0].data == backing && !bi->variable_arenas[0].first
        );
        flow_run_free(run);
        behavior_instance_destroy(bi);
    }
    arena_free(&storage);
}
static void lifecycle(behavior_backend backend)
{
    behavior_asset *a = whole_store_fixture(WHOLE_STORE_COPY);
    uint64_t id = a->variables[0].id;
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(bi, backend));
    behavior_instance_set_trace_enabled(bi, false);
    void *backing = bi->variable_storage[0].data;
    whole_record input = { .tag = 255,
                           .position = { 3, -4 },
                           .weight = 2.75,
                           .active = true,
                           .stamp = INT64_MAX };
    pin_value v = { .type = PIN_STRUCT, .type_name = "WholeRecord", .ptr = &input };
    assert(!behavior_instance_set_variable(bi, id, &v));
    arena storage = { 0 };
    pin_value retained;
    assert(!behavior_instance_copy_variable(bi, id, &storage, &retained));
    char *name = strdup("null nominal value");
    pin_value empty = { .type = PIN_STRUCT, .type_name = name };
    const behavior_function *f = behavior_program_function(
        behavior_program_graph(a->program, a->graphs[3]),
        a->graphs[3]->nodes[0]
    );
    assert(f);
    pin_value results[2] = { 0 };
    behavior_function_prepare_results(f, results, &storage);
    behavior_function_context ctx = { .arguments = &empty,
                                      .argument_count = 1,
                                      .results = results,
                                      .budget = 100 };
    behavior_instance_bind_values(bi, &ctx);
    assert(!behavior_function_run(f, &ctx, behavior_instance_backend(bi) == BEHAVIOR_SLJIT));
    memset(name, 'x', strlen(name));
    free(name);
    assert(!bi->variables[0].ptr && !strcmp(bi->variables[0].type_name, "null nominal value"));
    assert(bi->variable_storage[0].data == backing);
    same(retained, v);
    assert(!behavior_instance_set_variable(bi, id, &v) && bi->variables[0].ptr == backing);
    char *json = behavior_asset_save_json(a);
    behavior_asset *next = behavior_asset_load_json(json);
    free(json);
    assert(next);
    /* Stable IDs remap backing storage together with values. */
    behavior_variable swap = next->variables[0];
    next->variables[0] = next->variables[1];
    next->variables[1] = swap;
    assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY);
    behavior_instance_update(bi, next);
    behavior_asset_free(a);
    pin_value actual;
    assert(!behavior_instance_get_variable(bi, id, &actual));
    same(actual, v);
    assert(bi->variable_storage[1].data == backing && actual.ptr == backing);
    input.position.x = 11;
    graph *g = next->graphs[4];
    flow_options options = { .instance = bi };
    flow_error error = { 0 };
    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &v, 1 }, &options, &error);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    assert(run && !flow_run_resume(run) && run->whole_function_executed);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    same(bi->variables[1], v);
    assert(
        bi->variables[1].ptr == backing &&
        ((whole_record *)retained.ptr)->position.x == 3
    );
    flow_run_free(run);
    assert(!behavior_instance_pool_release(bi));
    assert(behavior_instance_pool_reuse(bi, next, NULL, NULL, NULL) == bi);
    assert(bi->variable_storage[1].data == backing && !bi->variables[1].ptr);
    input.position.x = 3;
    assert(!behavior_instance_set_variable(bi, id, &v));
    same(bi->variables[1], retained);
    arena_free(&storage);
    behavior_instance_destroy(bi);
    behavior_asset_free(next);
}
static void managed_storage()
{
    behavior_asset *a = behavior_asset_new("Managed variable storage");
    uint64_t id = behavior_asset_add_variable(a, "Parts", PIN_STRUCT, "GameMeshParts", PIN_CONTAINER_SINGLE, NULL, "")
                      ->id;
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(!bi->variable_storage[0].data);
    char text[] = "owned parts";
    GameMeshParts parts = { .parts = text };
    pin_value value = { .type = PIN_STRUCT, .type_name = "GameMeshParts", .ptr = &parts };
    assert(!behavior_instance_set_variable(bi, id, &value));
    text[0] = 'x';
    assert(!strcmp(((GameMeshParts *)bi->variables[0].ptr)->parts, "owned parts"));
    arena storage = { 0 };
    pin_value retained;
    assert(!behavior_instance_copy_variable(bi, id, &storage, &retained));
    char *json = behavior_asset_save_json(a);
    behavior_asset *next = behavior_asset_load_json(json);
    free(json);
    assert(next && behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_RESTART);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    behavior_asset_free(next);
    assert(!strcmp(retained.type_name, "GameMeshParts"));
    assert(!strcmp(((GameMeshParts *)retained.ptr)->parts, "owned parts"));
    arena_free(&storage);
}
void test_compiled_stores()
{
    for (int kind = 0; kind < WHOLE_STORE_COUNT; kind++) {
        behavior_asset *a = whole_store_fixture(kind);
        for (unsigned budget = 1; budget <= 17; budget++) {
            parity(a, false, budget);
            parity(a, true, budget);
        }
        behavior_asset_free(a);
    }
    lifecycle(BEHAVIOR_BYTECODE);
    lifecycle(behavior_default_backend());
    managed_storage();
    whole_store_smoke();
    puts("PASS: persistent struct ownership, nulls, snapshots, budget failures, hot apply and pool reuse");
}
