#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/suite_internal.h"
#include "test/whole_command_fixture.h"

static void same(pin_value a, pin_value b)
{
    assert(a.type == b.type && a.container == b.container);
    assert(!memcmp(&a.d, &b.d, behavior_scalar_size(a.type)));
    assert((a.type_name != NULL) == (b.type_name != NULL));
    if (a.type_name) {
        assert(!strcmp(a.type_name, b.type_name));
    }
}
static pin_value copy(arena *a, pin_value v)
{
    if (v.type_name) {
        char *name = arena_alloc(a, strlen(v.type_name) + 1);
        strcpy(name, v.type_name);
        v.type_name = name;
    }
    return v;
}
static void parity(int kind, int scenario, unsigned budget, bool subnormal)
{
    bool bad_input = kind == WHOLE_COMMAND_ARGUMENTS || kind == WHOLE_COMMAND_WRAPPED;
    behavior_asset *a = whole_command_fixture(kind, scenario == 2, bad_input && scenario != 0);
    graph *g = a->graphs[acount(a->graphs) - 1];
    pin_value items[] = { { .type = PIN_F64, .type_name = "first item", .d = -0. },
                          { .type = PIN_F64, .d = 3.5 },
                          { .type = PIN_F64, .type_name = "last item", .d = -2.25 } };
    pin_array array = { .element_type = PIN_F64,
                        .type_name = "double",
                        .items = items,
                        .count = 3,
                        .capacity = 3 };
    pin_value arguments[] = {
        { .type = PIN_I32, .type_name = "caller integer", .i = -123 },
        { .type = PIN_F64, .type_name = "caller real", .d = subnormal ? 0x1p-140 : 2.75 },
        { .type = PIN_F32, .type_name = "caller float", .f = -1.25f },
        { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = UINT64_C(0x123456789abcdef) },
        { .type = PIN_F64, .type_name = "double", .container = PIN_CONTAINER_ARRAY, .ptr = &array }
    };
    pin_value owner = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 456 };
    whole_command_capture expected = { 0 };
    arena storage = { 0 };
    pin_value variables[4] = { 0 }, returned[4] = { 0 };
    flow_error expected_error = { 0 };
    flow_call_frame frames[8];
    int status = 0, frame_count = 0;
    uintptr_t steps = 0;
    for (int mode = 0; mode < 3; mode++) {
        whole_command_capture actual = { .record = true,
                                         .fail_at = 2,
                                         .error = scenario == 3 ? FLOW_ERR_STATE : 0 };
        behavior_instance *bi = behavior_instance_create(a, &owner, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        assert(!behavior_instance_set_backend(
            bi,
            (behavior_backend[]){ BEHAVIOR_ORACLE,
                                  BEHAVIOR_BYTECODE,
                                  behavior_default_backend() }[mode]
        ));
        if (scenario != 1) {
            assert(!behavior_instance_bind_service(bi, "test.commands", &actual));
        }
        flow_options options = { .instance = bi,
                                 .step_budget = budget,
                                 .delta_seconds = .125 };
        flow_error error = { 0 };
        flow_run *run = flow_run_start(
            g,
            g->nodes[0],
            (flow_inputs){ arguments, kind == WHOLE_COMMAND_ARRAY ? 5 : 4 },
            &options,
            &error
        );
        assert(run);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        int result = flow_run_resume(run), count = 0;
        const flow_call_frame *stack = flow_run_call_stack(run, &count);
        if (!mode) {
            expected = actual;
            status = result;
            steps = flow_run_steps(run);
            expected_error = *flow_run_error(run);
            frame_count = count;
            assert(count <= 8);
            if (count) {
                memcpy(frames, stack, (size_t)count * sizeof *frames);
            }
            for (int i = 0; i < 4; i++) {
                variables[i] = copy(&storage, bi->variables[i]);
                if (!result) {
                    returned[i] = copy(&storage, *flow_run_input_value(run, g->nodes[1], i + 1));
                }
            }
        } else {
            assert(
                run->whole_function_executed && result == status &&
                flow_run_steps(run) == steps
            );
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            assert(actual.calls == expected.calls && actual.checksum == expected.checksum);
            assert(acount(actual.values) == acount(expected.values));
            for (int i = 0; i < acount(actual.values); i++) {
                same(actual.values[i], expected.values[i]);
            }
            for (int i = 0; i < 4; i++) {
                same(bi->variables[i], variables[i]);
                if (!result) {
                    same(*flow_run_input_value(run, g->nodes[1], i + 1), returned[i]);
                }
            }
            if (result) {
                const flow_error *e = flow_run_error(run);
                assert(
                    e->node == expected_error.node &&
                    e->pin == expected_error.pin && e->steps == expected_error.steps
                );
                if (strcmp(e->message, expected_error.message)) {
                    fprintf(
                        stderr,
                        "command kind=%d scenario=%d budget=%u mode=%d: %s "
                        "expected %s\n",
                        kind,
                        scenario,
                        budget,
                        mode,
                        e->message,
                        expected_error.message
                    );
                }
                assert(!strcmp(e->message, expected_error.message));
                assert(count == frame_count);
                for (int i = 0; i < count; i++) {
                    assert(
                        stack[i].graph_id == frames[i].graph_id &&
                        stack[i].node_id == frames[i].node_id
                    );
                }
            }
            whole_command_clear(&actual);
        }
        flow_run_free(run);
        /* Stored scalar names must survive release of activation input storage. */
        if (mode) {
            for (int i = 0; i < 4; i++) {
                same(bi->variables[i], variables[i]);
            }
        }
        behavior_instance_destroy(bi);
    }
    whole_command_clear(&expected);
    arena_free(&storage);
    behavior_asset_free(a);
}
static void scalar_widths()
{
    pin_value values[] = { { .type = PIN_BOOL, .b = true },
                           { .type = PIN_I8, .i8 = INT8_MIN },
                           { .type = PIN_U8, .u8 = UINT8_MAX },
                           { .type = PIN_I16, .i16 = INT16_MIN },
                           { .type = PIN_U16, .u16 = UINT16_MAX },
                           { .type = PIN_I32, .i = INT32_MIN },
                           { .type = PIN_U32, .u = UINT32_MAX },
                           { .type = PIN_I64, .i64 = INT64_MIN },
                           { .type = PIN_U64, .u64 = UINT64_MAX },
                           { .type = PIN_F32, .f = -0.f },
                           { .type = PIN_F64, .d = -0. },
                           { .type = PIN_HANDLE, .u64 = UINT64_MAX } };
    const char *names[] = { "bool",         "char",           "byte",
                            "short",        "unsigned short", "int",
                            "unsigned int", "int64",          "uint64",
                            "float",        "double",         "ecs.entity" };
    for (int k = 0; k < (int)(sizeof values / sizeof *values); k++) {
        behavior_asset *a = whole_command_fixture(WHOLE_COMMAND_ARGUMENTS, false, false);
        graph *g = a->graphs[1];
        node *entry = g->nodes[0], *ret = g->nodes[1], *send = g->nodes[2];
        assert(!graph_remove_edge(g, entry, 1, send, 1));
        assert(!graph_remove_edge(g, entry, 1, ret, 1));
        assert(!graph_set_pin_type(g, entry, 1, 1, values[k].type, names[k]));
        assert(!graph_set_pin_type(g, ret, 0, 1, values[k].type, names[k]));
        assert(!graph_set_pin_type(g, send, 0, 1, values[k].type, names[k]));
        assert(!graph_connect_data(g, entry, 1, send, 1));
        assert(!graph_connect_data(g, entry, 1, ret, 1));
        pin_value arg = values[k];
        arg.type_name = names[k];
        for (int mode = 0; mode < 3; mode++) {
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            behavior_instance_set_trace_enabled(bi, false);
            assert(!behavior_instance_set_backend(
                bi,
                (behavior_backend[]){ BEHAVIOR_ORACLE,
                                      BEHAVIOR_BYTECODE,
                                      behavior_default_backend() }[mode]
            ));
            whole_command_capture captured = { .record = true };
            assert(!behavior_instance_bind_service(bi, "test.commands", &captured));
            flow_options options = { .instance = bi };
            flow_run *run = flow_run_start(g, entry, (flow_inputs){ &arg, 1 }, &options, NULL);
            assert(run && !flow_run_resume(run));
            if (mode) {
                assert(run->whole_function_executed);
            }
            assert(captured.calls == 1);
            same(captured.values[0], arg);
            same(*flow_run_input_value(run, ret, 1), arg);
            flow_run_free(run);
            whole_command_clear(&captured);
            behavior_instance_destroy(bi);
        }
        behavior_asset_free(a);
    }
}

static void scalar_generation_ownership(behavior_backend backend)
{
    behavior_asset *a = behavior_asset_new("Scalar name generations");
    uint64_t id = behavior_asset_add_variable(a, "Value", PIN_F64, "double", PIN_CONTAINER_SINGLE, NULL, "")
                      ->id;
    graph *g = a->graph;
    node *entry = graph_add_node(g, &NODE_Event),
         *save = behavior_asset_add_set_node(a, id);
    assert(!graph_set_event_name(g, entry, "Run"));
    assert(!graph_set_pin_type(g, save, 0, 1, PIN_F64, "graph owned scalar name"));
    assert(!graph_set_pin_default(g, save, 0, 1, &(pin_value){ .type = PIN_F64, .d = 7.5 }));
    assert(!graph_connect_exec(g, entry, 0, save, 0, 0));
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, false);
    assert(!behavior_instance_set_backend(bi, backend));
    flow_options options = { .instance = bi };
    flow_run *run = flow_run_start(g, entry, (flow_inputs){ 0 }, &options, NULL);
    assert(run && !flow_run_resume(run));
    if (backend != BEHAVIOR_ORACLE) {
        assert(run->whole_function_executed);
    }
    flow_run_free(run);
    assert(
        bi->variables[0].d == 7.5 &&
        !strcmp(bi->variables[0].type_name, "graph owned scalar name")
    );
    char *json = behavior_asset_save(a);
    behavior_asset *replacement = behavior_asset_load(json);
    free(json);
    assert(replacement && behavior_instance_can_update(bi, replacement) == INSTANCE_UPDATE_READY);
    behavior_instance_update(bi, replacement);
    behavior_asset_free(a);
    assert(
        bi->variables[0].d == 7.5 &&
        !strcmp(bi->variables[0].type_name, "graph owned scalar name")
    );
    assert(!behavior_instance_pool_release(bi));
    assert(behavior_instance_pool_reuse(bi, replacement, NULL, NULL, NULL) == bi);
    assert(bi->variables[0].d == 0 && !strcmp(bi->variables[0].type_name, "double"));
    behavior_instance_destroy(bi);
    behavior_asset_free(replacement);
}

void test_compiled_commands()
{
    scalar_generation_ownership(BEHAVIOR_ORACLE);
    scalar_generation_ownership(BEHAVIOR_BYTECODE);
    scalar_generation_ownership(behavior_default_backend());
    scalar_widths();
    for (int kind = 0; kind < WHOLE_COMMAND_COUNT; kind++) {
        for (int scenario = 0; scenario < 4; scenario++) {
            for (unsigned budget = 1; budget <= 20; budget++) {
                parity(kind, scenario, budget, false);
            }
            parity(kind, scenario, 1000, false);
        }
    }
    parity(WHOLE_COMMAND_MIXED, 0, 1000, true);
    whole_command_smoke();
    test_compiled_entity_commands();
    puts("PASS: whole command payloads, scalar names, nested calls, loops, borrowed elements, snapshots, ownership, partial effects and error/source parity");
}
