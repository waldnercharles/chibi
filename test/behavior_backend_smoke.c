#include "graph/compiler/behavior_expression_wasm.h"
#include "graph/compiler/graph_jit_sljit.h"
#include "graph/model/registry.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/runtime/behavior_data.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/runtime.h"
#include "support/arena.h"
#include "support/cos_sin.h"
#include "test/instance_bank_fixture.h"
#include "test/whole_loop_fixture.h"
#include "test/whole_multigate_fixture.h"
#include "test/whole_name_fixture.h"
#include "test/whole_recursion_fixture.h"
#include "test/whole_retrigger_fixture.h"
#include "test/whole_search_fixture.h"
#include "test/whole_state_fixture.h"
#include "test/whole_switch_fixture.h"
#include "whole_aggregate_fixture.h"
#include "whole_alias_fixture.h"
#include "whole_array_fixture.h"
#include "whole_array_op_fixture.h"
#include "whole_async_fixture.h"
#include "whole_batch_fixture.h"
#include "whole_command_fixture.h"
#include "whole_data_fixture.h"
#include "whole_delay_fixture.h"
#include "whole_function_fixture.h"
#include "whole_integer_fixture.h"
#include "whole_managed_fixture.h"
#include "whole_math_fixture.h"
#include "whole_motion_fixture.h"
#include "whole_owned_array_fixture.h"
#include "whole_particle_fixture.h"
#include "whole_query_fixture.h"
#include "whole_random_fixture.h"
#include "whole_store_fixture.h"
#include "whole_suspend_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static const type_desc entity_type = { .name = "ecs.entity", .size = sizeof(uint64_t) };
static const type_field vector_fields[] = {
    { .name = "x", .type = PIN_F32, .type_name = "float", .offset = 0, .size = sizeof(float) },
    { .name = "y",
      .type = PIN_F32,
      .type_name = "float",
      .offset = sizeof(float),
      .size = sizeof(float) },
    { .name = "z",
      .type = PIN_F32,
      .type_name = "float",
      .offset = 2 * sizeof(float),
      .size = sizeof(float) }
};
static const type_desc vector_type = { .name = "CF_V3",
                                       .size = 3 * sizeof(float),
                                       .fields = vector_fields,
                                       .field_count = 3 };
static const type_desc vector2_type = { .name = "CF_V2",
                                        .size = 2 * sizeof(float),
                                        .fields = vector_fields,
                                        .field_count = 2 };
static const type_desc *const entity_types[] = { &entity_type, &vector_type, &vector2_type };
static const registry_pack entity_pack = { .name = "Entity identity",
                                           .types = entity_types,
                                           .type_count = 3 };

const registry_pack *const REGISTRY_PACKS[] = {
    &CORE_EXTRA_PACK,  &CORE_FLOW_PACK,     &WHOLE_VALUE_PACK,
    &WHOLE_ALIAS_PACK, &WHOLE_MANAGED_PACK, &entity_pack,
    &CORE_ARRAY_PACK,  &CORE_SET_MAP_PACK,  &CORE_STATE_PACK
};
const int REGISTRY_PACK_COUNT = sizeof REGISTRY_PACKS / sizeof REGISTRY_PACKS[0];

#ifdef __EMSCRIPTEN__
enum
{
    WASM_SMOKE_REGIONS = 4,
    WASM_SMOKE_LOOP_ITERATIONS = 2,
    WASM_SMOKE_BENCH_ITERATIONS = 4000,
    WASM_SMOKE_MAX_POLLS = 5000,
    WASM_SMOKE_MAX_OPERATIONS = 64,
};
static const int wasm_smoke_operations[WASM_SMOKE_REGIONS] = { 4, 16, 32, 64 };

static behavior_asset *wasm_smoke_asset;
static behavior_instance *wasm_smoke_instance;
static uint64_t wasm_smoke_input_id;
static uint64_t wasm_smoke_output_ids[WASM_SMOKE_REGIONS];
static uint64_t wasm_smoke_loop_output_id;
static node *wasm_smoke_sets[WASM_SMOKE_REGIONS];
static uint32_t wasm_smoke_execution_before;
static int wasm_smoke_polls;

static void wasm_smoke_poll(void *);
static void wasm_smoke_retirement_poll(void *);

static void wasm_smoke_retirement_poll(void *unused)
{
    (void)unused;
    assert(behavior_expression_wasm_registry_size() == 0);
    puts("browser behavior Wasm: async readiness, bytecode fallback, reuse, NaN/F32 parity, retirement");
    emscripten_force_exit(0);
}

static void wasm_smoke_dispatch()
{
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(wasm_smoke_instance, "Run", (flow_inputs){ 0 }, &error);
    flow_state status = run ? flow_run_status(run) : FLOW_IDLE;
    if (!run || status != FLOW_COMPLETED) {
        fprintf(
            stderr,
            "browser behavior Wasm dispatch: status=%d code=%d\n",
            status,
            error.code
        );
        fflush(stderr);
    }
    assert(run && status == FLOW_COMPLETED);
    assert(!behavior_instance_release_activation(wasm_smoke_instance, run));
}

static int wasm_smoke_reference_input(
    const behavior_graph_program *program,
    node *n,
    arena *storage,
    runtime_store *runtime,
    eval_context *context
)
{
    const graph_ir *ir = behavior_graph_ir(program);
    const graph_ir_block *block = NULL;
    for (int i = 0; i < acount(ir->blocks); i++) {
        const graph_ir_block *candidate = &ir->blocks[i];
        if (!candidate->output && candidate->node_id == n->id &&
            candidate->pin_id == n->input_pins[1].id) {
            block = candidate;
            break;
        }
    }
    assert(block);
    behavior_data_context data = { .program = program, .eval = context, .arena = storage };
    for (int pc = block->first; pc < block->first + block->count;) {
        const graph_ir_behavior_instruction *instruction = &ir->behavior[pc];
        int result = behavior_data_instruction(&data, instruction);
        if (instruction->op == GRAPH_IR_MEMO_BRANCH) {
            pc = result ? instruction->target : pc + 1;
            continue;
        }
        if (result) {
            return result;
        }
        pc++;
    }
    return 0;
}

static double wasm_smoke_bench(int region, bool wasm)
{
    behavior_instance_set_trace_enabled(wasm_smoke_instance, 0);
    const behavior_graph_program *program = behavior_program_graph(
        wasm_smoke_asset->program,
        wasm_smoke_asset->graph
    );
    arena storage = { 0 };
    runtime_store *runtime = runtime_store_create(wasm_smoke_asset->graph);
    eval_context context = { .instance = wasm_smoke_instance, .runtime = runtime };
    pin_value result;
    for (int i = 0; i < 100; i++) {
        runtime_store_reset_memo(runtime);
        assert(
            wasm ? !behavior_program_input(program, wasm_smoke_sets[region], 1, &storage, &result, &context, BEHAVIOR_BYTECODE)
                 : !wasm_smoke_reference_input(program, wasm_smoke_sets[region], &storage, runtime, &context)
        );
    }
    double start = emscripten_get_now();
    for (int i = 0; i < WASM_SMOKE_BENCH_ITERATIONS; i++) {
        runtime_store_reset_memo(runtime);
        assert(
            wasm ? !behavior_program_input(program, wasm_smoke_sets[region], 1, &storage, &result, &context, BEHAVIOR_BYTECODE)
                 : !wasm_smoke_reference_input(program, wasm_smoke_sets[region], &storage, runtime, &context)
        );
    }
    double elapsed = emscripten_get_now() - start;
    runtime_store_free(runtime);
    arena_free(&storage);
    return elapsed * 1000000.0 / WASM_SMOKE_BENCH_ITERATIONS;
}

static node *wasm_smoke_binary(graph *g, const node_desc *desc, node *left, node *right)
{
    node *operation = graph_add_node(g, desc);
    for (int pin = 0; pin < 2; pin++) {
        assert(!graph_set_pin_type(g, operation, false, pin, PIN_F32, "float"));
    }
    assert(!graph_set_pin_type(g, operation, true, 0, PIN_F32, "float"));
    assert(!graph_connect_data(g, left, 0, operation, 0));
    assert(!graph_connect_data(g, right, 0, operation, 1));
    return operation;
}

static node *wasm_smoke_expression(graph *g, node *input, int operations)
{
    assert(operations >= 4 && operations <= WASM_SMOKE_MAX_OPERATIONS);
    node *round = graph_add_node(g, &NODE_Add);
    for (int pin = 0; pin < 2; pin++) {
        assert(!graph_set_pin_type(g, round, false, pin, PIN_F32, "float"));
    }
    assert(!graph_set_pin_type(g, round, true, 0, PIN_F32, "float"));
    assert(
        !graph_set_pin_default(g, round, false, 1, &(pin_value){ .type = PIN_F32, .f = 1.0f })
    );
    assert(!graph_connect_data(g, input, 0, round, 0));
    node *result = wasm_smoke_binary(g, &NODE_Subtract, round, input);
    node *zero = wasm_smoke_binary(g, &NODE_Subtract, input, input);
    result = wasm_smoke_binary(g, &NODE_Add, result, zero);
    for (int i = 4; i < operations; i++) {
        result = wasm_smoke_binary(g, &NODE_Add, result, zero);
    }
    return result;
}

static void wasm_smoke_third_operand_regression()
{
    behavior_expression_wasm_instruction instructions[] = {
        { .op = BEHAVIOR_WASM_ADD,
          .type = PIN_F32,
          .output = 0,
          .left = 4,
          .right = 4,
          .third = -1,
          .left_slot = true,
          .right_slot = true },
        { .op = BEHAVIOR_WASM_ADD,
          .type = PIN_F32,
          .output = 1,
          .left = 0,
          .right = 4,
          .third = -1,
          .left_slot = true,
          .right_slot = true },
        { .op = BEHAVIOR_WASM_ADD,
          .type = PIN_F32,
          .output = 2,
          .left = 1,
          .right = 4,
          .third = -1,
          .left_slot = true,
          .right_slot = true },
        { .op = BEHAVIOR_WASM_ADD,
          .type = PIN_F32,
          .output = 3,
          .left = 2,
          .right = 4,
          .third = -1,
          .left_slot = true,
          .right_slot = true },
        /* BF_SELECT and BF_FMA both consume target as a third slot. */
        { .op = BEHAVIOR_WASM_NONE,
          .type = PIN_F32,
          .left = 3,
          .third = 0,
          .left_slot = true,
          .third_slot = true },
    };
    const pin_type slot_types[] = { PIN_F32, PIN_F32, PIN_F32, PIN_F32, PIN_F32 };
    behavior_expression_wasm_region *regions = NULL;
    int region_count = 0;
    behavior_expression_wasm *module = behavior_expression_wasm_compile_linear(
        instructions,
        sizeof instructions / sizeof instructions[0],
        sizeof slot_types / sizeof slot_types[0],
        slot_types,
        &regions,
        &region_count
    );
    assert(!module && !region_count && !regions);
}

static void wasm_smoke_branch_entry_regression()
{
    behavior_expression_wasm_instruction instructions[] = {
        { .op = BEHAVIOR_WASM_NONE, .control_target = 3, .control_transfer = true },
        { .op = BEHAVIOR_WASM_ADD,
          .type = PIN_F32,
          .output = 0,
          .left = 4,
          .right = 4,
          .left_slot = true,
          .right_slot = true },
        { .op = BEHAVIOR_WASM_ADD,
          .type = PIN_F32,
          .output = 1,
          .left = 0,
          .right = 4,
          .left_slot = true,
          .right_slot = true },
        { .op = BEHAVIOR_WASM_ADD,
          .type = PIN_F32,
          .output = 2,
          .left = 1,
          .right = 4,
          .left_slot = true,
          .right_slot = true },
        { .op = BEHAVIOR_WASM_ADD,
          .type = PIN_F32,
          .output = 3,
          .left = 2,
          .right = 4,
          .left_slot = true,
          .right_slot = true },
        { .op = BEHAVIOR_WASM_NONE, .left = 3, .left_slot = true },
    };
    const pin_type slot_types[] = { PIN_F32, PIN_F32, PIN_F32, PIN_F32, PIN_F32 };
    behavior_expression_wasm_region *regions = NULL;
    int region_count = 0;
    behavior_expression_wasm *module = behavior_expression_wasm_compile_linear(
        instructions,
        sizeof instructions / sizeof instructions[0],
        sizeof slot_types / sizeof slot_types[0],
        slot_types,
        &regions,
        &region_count
    );
    assert(!module && !region_count && !regions);
}

static void wasm_smoke_poll(void *unused)
{
    (void)unused;
    wasm_smoke_dispatch();
    behavior_expression_wasm_stats stats;
    behavior_expression_wasm_get_stats(&stats);
    if (stats.expressions_executed == wasm_smoke_execution_before) {
        assert(++wasm_smoke_polls < WASM_SMOKE_MAX_POLLS);
        emscripten_async_call(wasm_smoke_poll, NULL, 1);
        return;
    }
    for (int i = 0; i < WASM_SMOKE_REGIONS; i++) {
        pin_value result;
        assert(
            behavior_instance_get_variable(wasm_smoke_instance, wasm_smoke_output_ids[i], &result) == 0
        );
        assert(result.f == 0.0f);
    }
    pin_value loop_result;
    assert(
        behavior_instance_get_variable(wasm_smoke_instance, wasm_smoke_loop_output_id, &loop_result) == 0
    );
    assert(loop_result.f == 0.0f);

    uint32_t executions = stats.expressions_executed;
    wasm_smoke_dispatch();
    behavior_expression_wasm_get_stats(&stats);
    assert(stats.expressions_executed == executions + WASM_SMOKE_REGIONS + WASM_SMOKE_LOOP_ITERATIONS);

    assert(!behavior_instance_set_variable(
        wasm_smoke_instance,
        wasm_smoke_input_id,
        &(pin_value){ .type = PIN_F32, .f = NAN }
    ));
    behavior_instance_set_trace_enabled(wasm_smoke_instance, 1);
    wasm_smoke_dispatch();
    int trace_count = 0;
    behavior_instance_trace(wasm_smoke_instance, &trace_count);
    assert(trace_count > 0);
    for (int i = 0; i < WASM_SMOKE_REGIONS; i++) {
        pin_value result;
        assert(
            behavior_instance_get_variable(wasm_smoke_instance, wasm_smoke_output_ids[i], &result) == 0
        );
        assert(isnan(result.f));
    }
    assert(
        behavior_instance_get_variable(wasm_smoke_instance, wasm_smoke_loop_output_id, &loop_result) == 0
    );
    assert(isnan(loop_result.f));
    behavior_instance_set_trace_enabled(wasm_smoke_instance, 0);
    executions = stats.expressions_executed;
    wasm_smoke_dispatch();
    behavior_expression_wasm_get_stats(&stats);
    assert(stats.expressions_executed == executions + WASM_SMOKE_REGIONS + WASM_SMOKE_LOOP_ITERATIONS);

    printf("browser behavior expression timings (ns/input):");
    for (int i = 0; i < WASM_SMOKE_REGIONS; i++) {
        double bytecode_ns = wasm_smoke_bench(i, false);
        double wasm_ns = wasm_smoke_bench(i, true);
        printf(" %d ops %.1f/%.1f", wasm_smoke_operations[i], bytecode_ns, wasm_ns);
    }
    puts(" (bytecode/Wasm)");

    assert(behavior_expression_wasm_registry_size() == 2);
    uint32_t released = stats.modules_released;
    behavior_instance_destroy(wasm_smoke_instance);
    behavior_asset_free(wasm_smoke_asset);
    behavior_expression_wasm_get_stats(&stats);
    assert(stats.modules_released == released + 2);
    assert(behavior_expression_wasm_registry_size() == 0);
    emscripten_async_call(wasm_smoke_retirement_poll, NULL, 1);
}

static void wasm_smoke_linear_regression(void)
{
    behavior_asset *asset = behavior_asset_new("Browser linear expression stack");
    behavior_variable *input = behavior_asset_add_variable(
        asset,
        "Input",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32, .f = 16777216.0f },
        ""
    );
    uint64_t input_id = input->id;
    behavior_variable *output = behavior_asset_add_variable(
        asset,
        "Output",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32 },
        ""
    );
    uint64_t output_id = output->id;
    graph *g = asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    node *get = behavior_asset_add_get_node(asset, input_id);
    node *set = behavior_asset_add_set_node(asset, output_id);
    assert(!graph_set_event_name(g, event, "Run"));
    node *value = get;
    for (int i = 0; i < WASM_SMOKE_MAX_OPERATIONS; i++) {
        node *add = graph_add_node(g, &NODE_Add);
        for (int pin = 0; pin < 2; pin++) {
            assert(!graph_set_pin_type(g, add, false, pin, PIN_F32, "float"));
        }
        assert(!graph_set_pin_type(g, add, true, 0, PIN_F32, "float"));
        assert(
            !graph_set_pin_default(g, add, false, 1, &(pin_value){ .type = PIN_F32, .f = 1.0f })
        );
        assert(!graph_connect_data(g, value, 0, add, 0));
        value = add;
    }
    assert(!graph_connect_data(g, value, 0, set, 1));
    assert(!graph_connect_exec(g, event, 0, set, 0, 0));

    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    const behavior_graph_program *program = behavior_program_graph(asset->program, g);
    arena storage = { 0 };
    runtime_store *runtime = runtime_store_create(g);
    eval_context context = { .instance = instance, .runtime = runtime };
    pin_value expression;
    behavior_expression_wasm_stats before;
    behavior_expression_wasm_get_stats(&before);
    assert(
        !behavior_program_input(program, set, 1, &storage, &expression, &context, BEHAVIOR_BYTECODE)
    );
    behavior_expression_wasm_stats pending;
    behavior_expression_wasm_get_stats(&pending);
    assert(pending.bytecode_fallbacks > before.bytecode_fallbacks);
    assert(expression.f == 16777216.0f);
    runtime_store_free(runtime);
    arena_free(&storage);
    assert(behavior_graph_program_wasm_expression_count(program) >= 1);
    flow_error error = { 0 };
    flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    pin_value result;
    assert(!behavior_instance_get_variable(instance, output_id, &result));
    assert(result.f == 16777216.0f);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

static void wasm_smoke_start()
{
    wasm_smoke_third_operand_regression();
    wasm_smoke_branch_entry_regression();
    wasm_smoke_linear_regression();
    behavior_expression_wasm_stats before;
    behavior_expression_wasm_get_stats(&before);
    wasm_smoke_asset = behavior_asset_new("Browser expression Wasm");
    behavior_variable *input = behavior_asset_add_variable(
        wasm_smoke_asset,
        "Input",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32, .f = 16777216.0f },
        ""
    );
    wasm_smoke_input_id = input->id;
    graph *g = wasm_smoke_asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    node *get = behavior_asset_add_get_node(wasm_smoke_asset, wasm_smoke_input_id);
    assert(!graph_set_event_name(g, event, "Run"));
    behavior_variable *loop_output = behavior_asset_add_variable(
        wasm_smoke_asset,
        "LoopOutput",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32, .f = 0 },
        ""
    );
    wasm_smoke_loop_output_id = loop_output->id;
    node *loop = graph_add_node(g, &NODE_ForLoop);
    assert(!graph_set_pin_default(g, loop, false, 1, &(pin_value){ .type = PIN_I32, .i = 0 }));
    assert(
        !graph_set_pin_default(g, loop, false, 2, &(pin_value){ .type = PIN_I32, .i = WASM_SMOKE_LOOP_ITERATIONS - 1 })
    );
    node *loop_set = behavior_asset_add_set_node(wasm_smoke_asset, wasm_smoke_loop_output_id);
    node *loop_value = wasm_smoke_expression(g, get, wasm_smoke_operations[1]);
    assert(!graph_connect_data(g, loop_value, 0, loop_set, 1));
    assert(!graph_connect_exec(g, event, 0, loop, 0, 0));
    assert(!graph_connect_exec(g, loop, 0, loop_set, 0, 0));
    for (int i = 0; i < WASM_SMOKE_REGIONS; i++) {
        char output_name[16];
        snprintf(output_name, sizeof output_name, "Output%d", i);
        behavior_variable *output = behavior_asset_add_variable(
            wasm_smoke_asset,
            output_name,
            PIN_F32,
            "float",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_F32, .f = 0 },
            ""
        );
        wasm_smoke_output_ids[i] = output->id;
        wasm_smoke_sets[i] = behavior_asset_add_set_node(wasm_smoke_asset, wasm_smoke_output_ids[i]);
        node *value = wasm_smoke_expression(g, get, wasm_smoke_operations[i]);
        assert(!graph_connect_data(g, value, 0, wasm_smoke_sets[i], 1));
        assert(
            !graph_connect_exec(g, i ? wasm_smoke_sets[i - 1] : loop, i ? 0 : 2, wasm_smoke_sets[i], 0, 0)
        );
    }
    wasm_smoke_instance = behavior_instance_create(wasm_smoke_asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(wasm_smoke_instance, 0);
    const behavior_graph_program
        *program = behavior_program_graph(wasm_smoke_asset->program, g);
    assert(behavior_graph_program_wasm_expression_count(program) >= WASM_SMOKE_REGIONS);
    behavior_expression_wasm_stats compiled;
    behavior_expression_wasm_get_stats(&compiled);
    assert(compiled.modules_requested == before.modules_requested + 2);
    assert(compiled.expressions_eligible >= before.expressions_eligible + 2 * WASM_SMOKE_REGIONS);
    wasm_smoke_dispatch();
    for (int i = 0; i < WASM_SMOKE_REGIONS; i++) {
        pin_value result;
        assert(
            behavior_instance_get_variable(wasm_smoke_instance, wasm_smoke_output_ids[i], &result) == 0
        );
        assert(result.f == 0.0f);
    }
    behavior_expression_wasm_get_stats(&compiled);
    assert(compiled.bytecode_fallbacks > before.bytecode_fallbacks);
    assert(compiled.expressions_executed == before.expressions_executed);
    wasm_smoke_execution_before = compiled.expressions_executed;
    emscripten_async_call(wasm_smoke_poll, NULL, 1);
}
#endif

int main()
{
    behavior_asset *asset = behavior_asset_new("Backend smoke");
    behavior_variable *variable = behavior_asset_add_variable(
        asset,
        "Value",
        PIN_F64,
        "double",
        PIN_CONTAINER_SINGLE,
        NULL,
        ""
    );
    graph *g = asset->graph;
    node *entry = graph_add_node(g, &NODE_Event);
    node *delay = graph_add_node(g, &NODE_Delay);
    node *write = behavior_asset_add_set_node(asset, variable->id);
    assert(graph_set_event_name(g, entry, "Run") == 0);
    assert(graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .5 }) == 0);
    assert(graph_set_pin_default(g, write, 0, 1, &(pin_value){ .type = PIN_F64, .d = 42 }) == 0);
    assert(graph_connect_exec(g, entry, 0, delay, 0, 0) == 0);
    assert(graph_connect_exec(g, delay, 0, write, 0, 0) == 0);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    flow_error error = { 0 };
    uint64_t eval_before = eval_oracle_calls(), flow_before = flow_oracle_calls();
    flow_run *run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
#ifdef __EMSCRIPTEN__
    assert(!graph_sljit_available());
    assert(behavior_instance_backend(instance) == BEHAVIOR_BYTECODE);
#else
    if (!graph_sljit_available()) {
        assert(!run && error.code == FLOW_ERR_COMPILE);
        assert(strstr(error.message, "requires SLJIT"));
        assert(behavior_instance_backend(instance) == BEHAVIOR_UNAVAILABLE);
        assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
        behavior_instance_destroy(instance);
        behavior_asset_free(asset);
        puts("UNAVAILABLE: native execution rejected without interpreter fallback");
        return 0;
    }
    assert(behavior_instance_backend(instance) == BEHAVIOR_SLJIT);
#endif
    if (!run) {
        fprintf(stderr, "%s\n", error.message);
    }
    assert(run && flow_run_status(run) == FLOW_SUSPENDED);
    assert(behavior_instance_tick(instance, .25) == FLOW_OK);
    assert(flow_run_status(run) == FLOW_SUSPENDED);
    assert(behavior_instance_tick(instance, .5) == FLOW_OK);
    assert(flow_run_status(run) == FLOW_COMPLETED);
    pin_value value;
    assert(behavior_instance_get_variable(instance, variable->id, &value) == 0 && value.d == 42);
    assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    printf(
        "%s: event, yield, resume and variable write; zero oracle calls\n",
        behavior_backend_name(behavior_instance_backend(instance))
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    whole_aggregate_smoke();
    whole_batch_smoke();
    instance_bank_smoke(behavior_default_backend());
    whole_async_smoke();
    whole_motion_smoke();
    whole_data_smoke();
    whole_alias_smoke();
    whole_store_smoke();
    whole_array_smoke();
    whole_query_smoke();
    asset = whole_function_fixture();
    instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED && flow_run_steps(run) == 14);
    assert(
        behavior_instance_get_variable(instance, asset->variables[0].id, &value) == 0 &&
        value.f == 4
    );
    assert(
        behavior_instance_get_variable(instance, asset->variables[1].id, &value) == 0 &&
        value.f == 8
    );
    assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    printf(
        "%s: whole-function loop, sequence and branch; zero oracle calls\n",
        behavior_backend_name(behavior_instance_backend(instance))
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    asset = whole_arguments_fixture();
    instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    graph *function = asset->graphs[1];
    assert(behavior_program_function(
        behavior_program_graph(asset->program, function),
        graph_node_by_id(function, function->entry_id)
    ));
    run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    assert(
        instance->variables[0].d == 3.25 && instance->variables[1].f == -2.5f &&
        instance->variables[2].b
    );
    assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    printf(
        "%s: nested whole-function arguments, conversions, min/max and Return; "
        "zero oracle calls\n",
        behavior_backend_name(behavior_instance_backend(instance))
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    asset = whole_pure_fixture();
    instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    instance->variables[0].d = 4;
    arena storage = { 0 };
    eval_context ctx = { .instance = instance,
                         .runtime = runtime_store_create(asset->graph) };
    uint64_t allocations = runtime_store_creations();
    assert(
        behavior_program_output(
            behavior_program_graph(asset->program, asset->graph),
            asset->graph->nodes[0],
            0,
            &storage,
            &value,
            &ctx,
            behavior_instance_backend(instance)
        ) == 0
    );
    assert(value.d == 9.25 && runtime_store_creations() == allocations);
    assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    printf(
        "%s: nested pure bodies; no callee allocation or oracle calls\n",
        behavior_backend_name(behavior_instance_backend(instance))
    );
    runtime_store_free(ctx.runtime);
    arena_free(&storage);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    asset = whole_math_fixture();
    instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    double sine = cos_sin_sine(.1), cosine = cos_sin_cosine(sine);
    for (int i = 1; i < 6; i++) {
        double x = cos_sin_sine(.1 + i * .2);
        sine += x;
        cosine += cos_sin_cosine(x);
    }
    run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    assert(
        instance->variables[0].d == sine + cosine &&
        instance->variables[1].f == -.5f && instance->variables[2].b
    );
    assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    printf(
        "%s: whole math calls and scalar results; zero oracle calls\n",
        behavior_backend_name(behavior_instance_backend(instance))
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    for (int wide = 0; wide < 2; wide++) {
        asset = whole_integer_fixture(wide ? PIN_I64 : PIN_I32, false);
        instance = behavior_instance_create(asset, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(instance, 0);
        assert(behavior_program_function(
            behavior_program_graph(asset->program, asset->graph),
            asset->graph->nodes[0]
        ));
        run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
        assert(run && flow_run_status(run) == FLOW_COMPLETED);
        assert(instance->variables[0].i == 1);
        assert(
            wide ? instance->variables[1].i64 == 5 && instance->variables[2].i64 == 2 &&
                       instance->variables[3].i64 == 292
                 : instance->variables[1].i == 5 && instance->variables[2].i == 2 &&
                       instance->variables[3].i == 292
        );
        behavior_instance_destroy(instance);
        behavior_asset_free(asset);
    }
    asset = whole_integer_loop();
    instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    run = behavior_instance_dispatch(instance, "Run", (flow_inputs){ 0 }, &error);
    assert(
        run && flow_run_status(run) == FLOW_COMPLETED && instance->variables[0].i == 4
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    asset = whole_integer_operation(&NODE_To_Integer64, PIN_F64, PIN_I64);
    instance = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(instance, 0);
    instance->variables[0].d = 0x1p63;
    ctx = (eval_context){ .instance = instance,
                          .runtime = runtime_store_create(asset->graph) };
    assert(!behavior_program_output(
        behavior_program_graph(asset->program, asset->graph),
        asset->graph->nodes[0],
        0,
        &storage,
        &value,
        &ctx,
        behavior_instance_backend(instance)
    ));
    assert(value.i64 == INT64_MAX);
    assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    printf(
        "%s: whole integer bodies, loop and saturating wide cast; zero oracle "
        "calls\n",
        behavior_backend_name(behavior_instance_backend(instance))
    );
    runtime_store_free(ctx.runtime);
    arena_free(&storage);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    whole_component_smoke();
    whole_loop_smoke();
    whole_switch_smoke();
    whole_state_smoke();
    whole_multigate_smoke();
    whole_command_smoke();
    whole_suspend_smoke();
    whole_delay_smoke();
    whole_retrigger_smoke();
    whole_recursion_smoke();
    whole_random_smoke();
    whole_particle_smoke();
    whole_managed_smoke();
    whole_owned_array_smoke();
    whole_array_op_smoke();
    whole_name_smoke();
    whole_search_smoke();
#ifdef __EMSCRIPTEN__
    wasm_smoke_start();
    emscripten_exit_with_live_runtime();
#endif
    return 0;
}
