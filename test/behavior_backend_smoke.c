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
#ifdef __EMSCRIPTEN_PTHREADS__
#include <emscripten/threading.h>
#include <pthread.h>
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
    WASM_SMOKE_REGIONS = 8,
    WASM_SMOKE_ELIGIBLE_REGIONS = 6,
    WASM_SMOKE_MAX_INPUTS = 8,
    WASM_SMOKE_LOOP_ITERATIONS = 2,
    WASM_SMOKE_BENCH_ITERATIONS = 4000,
    WASM_SMOKE_WORKER_TRIALS = 5,
    WASM_SMOKE_WORKER_HANDOFFS = 128,
    WASM_SMOKE_MAX_POLLS = 5000,
    WASM_SMOKE_MAX_OPERATIONS = 64,
};
static const int wasm_smoke_operations[WASM_SMOKE_REGIONS] = { 12, 16, 16, 24,
                                                               32, 16, 64, 16 };
static const int wasm_smoke_inputs[WASM_SMOKE_REGIONS] = { 1, 1, 4, 6,
                                                           8, 8, 1, 1 };
static const bool wasm_smoke_rounding[WASM_SMOKE_REGIONS] = {
    false, false, false, false, false, false, false, true
};

static behavior_asset *wasm_smoke_asset;
static behavior_instance *wasm_smoke_instance;
static uint64_t wasm_smoke_input_ids[WASM_SMOKE_MAX_INPUTS];
static uint64_t wasm_smoke_output_ids[WASM_SMOKE_REGIONS];
static uint64_t wasm_smoke_loop_output_id;
static node *wasm_smoke_sets[WASM_SMOKE_REGIONS];
static node *wasm_smoke_loop_set;
static node *wasm_smoke_input_gets[WASM_SMOKE_MAX_INPUTS];
static int wasm_smoke_polls;
static const float wasm_smoke_rounding_input_value = 16777216.0f;

static void wasm_smoke_poll(void *);
static void wasm_smoke_retirement_poll(void *);

static void wasm_smoke_retirement_poll(void *unused)
{
    (void)unused;
    assert(behavior_expression_wasm_registry_size() == 0);
    puts("browser behavior Wasm: async readiness, bytecode fallback, reuse, NaN/F32 rounding parity, retirement");
    emscripten_force_exit(0);
}

static float wasm_smoke_input_value(int input)
{
    return input == 0 ? wasm_smoke_rounding_input_value : (float)input;
}

static float wasm_smoke_expected(int region)
{
    if (wasm_smoke_rounding[region]) {
        return 0.0f;
    }
    float result = wasm_smoke_input_value(0);
    int input_count = wasm_smoke_inputs[region];
    for (int i = 0; i < wasm_smoke_operations[region]; i++) {
        result = (float)(result + wasm_smoke_input_value(i % input_count));
    }
    return result;
}

static void wasm_smoke_restore_inputs(void)
{
    for (int i = 0; i < WASM_SMOKE_MAX_INPUTS; i++) {
        assert(!behavior_instance_set_variable(
            wasm_smoke_instance,
            wasm_smoke_input_ids[i],
            &(pin_value){ .type = PIN_F32, .f = wasm_smoke_input_value(i) }
        ));
    }
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
    eval_context *context,
    pin_value *out
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
    *out = runtime_node_inputs(runtime_store_node(runtime, n))[1];
    return 0;
}

static double wasm_smoke_bench(int region, bool wasm)
{
    behavior_instance_set_trace_enabled(wasm_smoke_instance, 0);
    wasm_smoke_restore_inputs();
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
            wasm
                ? !behavior_program_input(program, wasm_smoke_sets[region], 1, &storage, &result, &context, BEHAVIOR_BYTECODE)
                : !wasm_smoke_reference_input(program, wasm_smoke_sets[region], &storage, runtime, &context, &result)
        );
    }
    double start = emscripten_get_now();
    for (int i = 0; i < WASM_SMOKE_BENCH_ITERATIONS; i++) {
        runtime_store_reset_memo(runtime);
        assert(
            wasm
                ? !behavior_program_input(program, wasm_smoke_sets[region], 1, &storage, &result, &context, BEHAVIOR_BYTECODE)
                : !wasm_smoke_reference_input(program, wasm_smoke_sets[region], &storage, runtime, &context, &result)
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

static node *wasm_smoke_expression(graph *g, node *const *inputs, int operations, int input_count)
{
    assert(operations >= 1 && operations <= WASM_SMOKE_MAX_OPERATIONS);
    assert(input_count >= 1 && input_count <= WASM_SMOKE_MAX_INPUTS);
    node *result = inputs[0];
    for (int i = 0; i < operations; i++) {
        result = wasm_smoke_binary(g, &NODE_Add, result, inputs[i % input_count]);
    }
    return result;
}

static node *wasm_smoke_rounding_expression(graph *g, node *input, int operations)
{
    assert(operations >= 2 && operations <= WASM_SMOKE_MAX_OPERATIONS);
    /* At 2^24, F32 rounds input + 1 back to input; a wider intermediate yields 1. */
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
    for (int i = 2; i < operations; i++) {
        node *add_zero = graph_add_node(g, &NODE_Add);
        for (int pin = 0; pin < 2; pin++) {
            assert(!graph_set_pin_type(g, add_zero, false, pin, PIN_F32, "float"));
        }
        assert(!graph_set_pin_type(g, add_zero, true, 0, PIN_F32, "float"));
        assert(
            !graph_set_pin_default(g, add_zero, false, 1, &(pin_value){ .type = PIN_F32, .f = 0.0f })
        );
        assert(!graph_connect_data(g, result, 0, add_zero, 0));
        result = add_zero;
    }
    return result;
}

static void wasm_smoke_region_selection()
{
    enum
    {
        operations = 32,
        input = operations,
        slot_count = operations + 1
    };
    behavior_expression_wasm_instruction instructions[operations + 2] = { 0 };
    pin_type slot_types[slot_count];
    for (int i = 0; i < slot_count; i++) {
        slot_types[i] = PIN_F32;
    }
    for (int i = 0; i < operations; i++) {
        instructions[i] = (behavior_expression_wasm_instruction){
            .op = BEHAVIOR_WASM_ADD,
            .type = PIN_F32,
            .output = i,
            .left = i ? i - 1 : input,
            .right = input,
            .left_slot = true,
            .right_slot = true,
        };
    }
    instructions[operations].left = operations - 1;
    instructions[operations].left_slot = true;
    for (int scenario = 0; scenario < 4; scenario++) {
        int count = operations + 1;
        if (scenario == 0) {
            // A short chain stays in bytecode even when all other checks pass.
            instructions[8].op = BEHAVIOR_WASM_NONE;
            instructions[8].left = 7;
            count = 9;
        } else {
            instructions[8].op = BEHAVIOR_WASM_ADD;
            instructions[8].left = 7;
        }
        // Another instruction needs an intermediate: it must remain materialized.
        instructions[operations].third_slot = scenario == 1;
        instructions[operations].third = 0;
        // A branch entering the region must execute the original instruction stream.
        instructions[operations + 1].control_transfer = scenario == 2;
        instructions[operations + 1].control_target = 3;
        if (scenario == 2) {
            count++;
        }
        behavior_expression_wasm_region *regions = NULL;
        int region_count = 0;
        behavior_expression_wasm *module = behavior_expression_wasm_compile_linear(
            instructions,
            count,
            slot_count,
            slot_types,
            &regions,
            &region_count
        );
        if (scenario < 3) {
            assert(!module && !region_count && !regions);
        } else {
            assert(module && region_count == 1 && regions[0].first == 0);
            assert(regions[0].end == operations && regions[0].result_slot == operations - 1);
            behavior_expression_wasm_free(module);
            afree(regions);
        }
    }
}

static void wasm_smoke_poll(void *unused)
{
    (void)unused;
    behavior_expression_wasm_stats stats;
    behavior_expression_wasm_get_stats(&stats);
    uint32_t before_dispatch = stats.expressions_executed;
    wasm_smoke_dispatch();
    behavior_expression_wasm_get_stats(&stats);
    uint32_t dispatch_executions = stats.expressions_executed - before_dispatch;
    if (dispatch_executions < WASM_SMOKE_ELIGIBLE_REGIONS) {
        assert(++wasm_smoke_polls < WASM_SMOKE_MAX_POLLS);
        emscripten_async_call(wasm_smoke_poll, NULL, 1);
        return;
    }
    assert(dispatch_executions == WASM_SMOKE_ELIGIBLE_REGIONS);
    for (int i = 0; i < WASM_SMOKE_REGIONS; i++) {
        pin_value result;
        assert(
            behavior_instance_get_variable(wasm_smoke_instance, wasm_smoke_output_ids[i], &result) == 0
        );
        assert(result.f == wasm_smoke_expected(i));
    }
    pin_value loop_result;
    assert(
        behavior_instance_get_variable(wasm_smoke_instance, wasm_smoke_loop_output_id, &loop_result) == 0
    );
    assert(loop_result.f == wasm_smoke_expected(1));

    uint32_t executions = stats.expressions_executed;
    wasm_smoke_dispatch();
    behavior_expression_wasm_get_stats(&stats);
    assert(stats.expressions_executed == executions + WASM_SMOKE_ELIGIBLE_REGIONS);

    const behavior_graph_program *program = behavior_program_graph(
        wasm_smoke_asset->program,
        wasm_smoke_asset->graph
    );
    runtime_store *runtime = runtime_store_create(wasm_smoke_asset->graph);
    arena storage = { 0 };
    eval_context context = { .instance = wasm_smoke_instance, .runtime = runtime };
    executions = stats.expressions_executed;
    for (int i = 0; i < WASM_SMOKE_LOOP_ITERATIONS; i++) {
        runtime_store_reset_memo(runtime);
        pin_value result;
        assert(
            !behavior_program_input(program, wasm_smoke_loop_set, 1, &storage, &result, &context, BEHAVIOR_BYTECODE)
        );
        assert(result.f == wasm_smoke_expected(1));
    }
    behavior_expression_wasm_get_stats(&stats);
    assert(stats.expressions_executed == executions + WASM_SMOKE_LOOP_ITERATIONS);
    runtime_store_free(runtime);
    arena_free(&storage);

    for (int i = 0; i < WASM_SMOKE_MAX_INPUTS; i++) {
        assert(!behavior_instance_set_variable(
            wasm_smoke_instance,
            wasm_smoke_input_ids[i],
            &(pin_value){ .type = PIN_F32, .f = NAN }
        ));
    }
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
    assert(stats.expressions_executed == executions + WASM_SMOKE_ELIGIBLE_REGIONS);
    for (int i = 0; i < WASM_SMOKE_REGIONS; i++) {
        pin_value result;
        assert(
            behavior_instance_get_variable(wasm_smoke_instance, wasm_smoke_output_ids[i], &result) == 0
        );
        assert(isnan(result.f));
    }

    printf("browser behavior expression timings (ns/input):");
    for (int i = 0; i < WASM_SMOKE_REGIONS; i++) {
        double bytecode_ns = wasm_smoke_bench(i, false);
        double wasm_ns = wasm_smoke_bench(i, true);
        printf(
            " %d ops/%d inputs%s %.1f/%.1f",
            wasm_smoke_operations[i],
            wasm_smoke_inputs[i],
            wasm_smoke_rounding[i] ? " rounding" : "",
            bytecode_ns,
            wasm_ns
        );
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

#ifdef __EMSCRIPTEN_PTHREADS__
enum
{
    WASM_WORKER_OPERATIONS = 16,
    WASM_WORKER_CACHE_CAPACITY = 16,
    WASM_WORKER_CACHE_TEST_MODULES = WASM_WORKER_CACHE_CAPACITY + 1,
    WASM_WORKER_SLOT_COUNT = WASM_WORKER_OPERATIONS + 2,
};

typedef struct
{
    bool ran;
    pin_value result;
    int cache_size;
    double worker_ms;
    double caller_ms;
    uint32_t wasm_executions;
} wasm_worker_result;

static pthread_mutex_t wasm_worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wasm_worker_condition = PTHREAD_COND_INITIALIZER;
static int wasm_worker_state;
static behavior_expression_wasm *wasm_worker_job_module;
static pin_value wasm_worker_job_slots[WASM_WORKER_SLOT_COUNT];
static bool wasm_worker_job_graph;
static bool wasm_worker_job_graph_wasm;
static int wasm_worker_job_region;
static int wasm_worker_job_iterations;
static const behavior_graph_program *wasm_worker_job_program;
static arena *wasm_worker_job_storage;
static runtime_store *wasm_worker_job_runtime;
static eval_context *wasm_worker_job_context;
static wasm_worker_result wasm_worker_job_result;

static behavior_expression_wasm *wasm_smoke_worker_module(
    behavior_expression_wasm_op op,
    behavior_expression_wasm_region **regions
)
{
    behavior_expression_wasm_instruction instructions[WASM_WORKER_OPERATIONS + 1] = { 0 };
    pin_type slot_types[WASM_WORKER_SLOT_COUNT];
    for (int i = 0; i < WASM_WORKER_SLOT_COUNT; i++) {
        slot_types[i] = PIN_F32;
    }
    for (int i = 0; i < WASM_WORKER_OPERATIONS; i++) {
        instructions[i] = (behavior_expression_wasm_instruction){
            .op = op,
            .type = PIN_F32,
            .output = i,
            .left = i ? i - 1 : WASM_WORKER_OPERATIONS,
            .right = WASM_WORKER_OPERATIONS + 1,
            .left_slot = true,
            .right_slot = true,
        };
    }
    instructions[WASM_WORKER_OPERATIONS] = (behavior_expression_wasm_instruction){
        .left = WASM_WORKER_OPERATIONS - 1,
        .left_slot = true,
    };
    int region_count = 0;
    behavior_expression_wasm *module = behavior_expression_wasm_compile_linear(
        instructions,
        WASM_WORKER_OPERATIONS + 1,
        WASM_WORKER_SLOT_COUNT,
        slot_types,
        regions,
        &region_count
    );
    assert(module && region_count == 1);
    assert((*regions)[0].first == 0 && (*regions)[0].end == WASM_WORKER_OPERATIONS);
    assert((*regions)[0].function == 0 && (*regions)[0].result_slot == WASM_WORKER_OPERATIONS - 1);
    return module;
}

static void *wasm_smoke_worker(void *unused)
{
    (void)unused;
    assert(!emscripten_is_main_runtime_thread());
    for (;;) {
        pthread_mutex_lock(&wasm_worker_mutex);
        while (wasm_worker_state != 1 && wasm_worker_state != 3) {
            pthread_cond_wait(&wasm_worker_condition, &wasm_worker_mutex);
        }
        if (wasm_worker_state == 3) {
            pthread_mutex_unlock(&wasm_worker_mutex);
            return NULL;
        }
        behavior_expression_wasm *module = wasm_worker_job_module;
        bool graph_job = wasm_worker_job_graph;
        bool graph_wasm = wasm_worker_job_graph_wasm;
        int region = wasm_worker_job_region;
        int iterations = wasm_worker_job_iterations;
        const behavior_graph_program *program = wasm_worker_job_program;
        arena *storage = wasm_worker_job_storage;
        runtime_store *runtime = wasm_worker_job_runtime;
        eval_context *context = wasm_worker_job_context;
        pin_value slots[WASM_WORKER_SLOT_COUNT];
        memcpy(slots, wasm_worker_job_slots, sizeof slots);
        pthread_mutex_unlock(&wasm_worker_mutex);

        wasm_worker_result result = { 0 };
        if (graph_job) {
            double start = emscripten_get_now();
            result.ran = true;
            for (int i = 0; i < iterations; i++) {
                runtime_store_reset_memo(runtime);
                int status = graph_wasm ? behavior_program_input(
                                              program,
                                              wasm_smoke_sets[region],
                                              1,
                                              storage,
                                              &result.result,
                                              context,
                                              BEHAVIOR_BYTECODE
                                          )
                                        : wasm_smoke_reference_input(
                                              program,
                                              wasm_smoke_sets[region],
                                              storage,
                                              runtime,
                                              context,
                                              &result.result
                                          );
                if (status) {
                    result.ran = false;
                    break;
                }
            }
            result.worker_ms = emscripten_get_now() - start;
        } else {
            result.ran = behavior_expression_wasm_run_slots(
                module,
                0,
                slots,
                &result.result
            );
        }
        result.cache_size = behavior_expression_wasm_registry_size();
        module = NULL;
        memset(slots, 0, sizeof slots);

        pthread_mutex_lock(&wasm_worker_mutex);
        wasm_worker_job_module = NULL;
        wasm_worker_job_program = NULL;
        wasm_worker_job_storage = NULL;
        wasm_worker_job_runtime = NULL;
        wasm_worker_job_context = NULL;
        wasm_worker_job_result = result;
        wasm_worker_state = 2;
        pthread_cond_broadcast(&wasm_worker_condition);
        pthread_mutex_unlock(&wasm_worker_mutex);
    }
}

static wasm_worker_result wasm_smoke_worker_run(behavior_expression_wasm *module, const pin_value *slots)
{
    double start = emscripten_get_now();
    pthread_mutex_lock(&wasm_worker_mutex);
    assert(wasm_worker_state == 0);
    wasm_worker_job_module = module;
    wasm_worker_job_graph = false;
    memcpy(wasm_worker_job_slots, slots, sizeof wasm_worker_job_slots);
    wasm_worker_state = 1;
    pthread_cond_broadcast(&wasm_worker_condition);
    while (wasm_worker_state != 2) {
        pthread_cond_wait(&wasm_worker_condition, &wasm_worker_mutex);
    }
    wasm_worker_result result = wasm_worker_job_result;
    wasm_worker_state = 0;
    pthread_cond_broadcast(&wasm_worker_condition);
    pthread_mutex_unlock(&wasm_worker_mutex);
    result.caller_ms = emscripten_get_now() - start;
    return result;
}

static wasm_worker_result wasm_smoke_worker_run_graph(
    bool wasm,
    int region,
    int iterations,
    const behavior_graph_program *program,
    arena *storage,
    runtime_store *runtime,
    eval_context *context
)
{
    behavior_expression_wasm_stats before, after;
    behavior_expression_wasm_get_stats(&before);
    double start = emscripten_get_now();
    pthread_mutex_lock(&wasm_worker_mutex);
    assert(wasm_worker_state == 0);
    wasm_worker_job_graph = true;
    wasm_worker_job_graph_wasm = wasm;
    wasm_worker_job_region = region;
    wasm_worker_job_iterations = iterations;
    wasm_worker_job_program = program;
    wasm_worker_job_storage = storage;
    wasm_worker_job_runtime = runtime;
    wasm_worker_job_context = context;
    wasm_worker_state = 1;
    pthread_cond_broadcast(&wasm_worker_condition);
    while (wasm_worker_state != 2) {
        pthread_cond_wait(&wasm_worker_condition, &wasm_worker_mutex);
    }
    wasm_worker_result result = wasm_worker_job_result;
    wasm_worker_state = 0;
    pthread_cond_broadcast(&wasm_worker_condition);
    pthread_mutex_unlock(&wasm_worker_mutex);
    result.caller_ms = emscripten_get_now() - start;
    behavior_expression_wasm_get_stats(&after);
    result.wasm_executions = after.expressions_executed - before.expressions_executed;
    return result;
}

static double wasm_smoke_median(double *values, int count)
{
    double sorted[WASM_SMOKE_WORKER_TRIALS];
    assert(count <= WASM_SMOKE_WORKER_TRIALS);
    memcpy(sorted, values, (size_t)count * sizeof *sorted);
    for (int i = 1; i < count; i++) {
        double value = sorted[i];
        int at = i;
        while (at && sorted[at - 1] > value) {
            sorted[at] = sorted[at - 1];
            at--;
        }
        sorted[at] = value;
    }
    return sorted[count / 2];
}

static bool wasm_smoke_region_eligible(int region)
{
    return wasm_smoke_operations[region] >= 16 &&
           wasm_smoke_operations[region] >= wasm_smoke_inputs[region] * 4;
}

static void wasm_smoke_worker_benchmark(const behavior_graph_program *program)
{
    behavior_instance_set_trace_enabled(wasm_smoke_instance, 0);
    wasm_smoke_restore_inputs();
    arena storage = { 0 };
    runtime_store *runtime = runtime_store_create(wasm_smoke_asset->graph);
    eval_context context = { .instance = wasm_smoke_instance, .runtime = runtime };

    /* Synthetic module retirement above advances the worker epoch. This first
     * graph call therefore measures a worker's first module copy/compile. */
    int cold_region = 1;
    wasm_worker_result
        cold = wasm_smoke_worker_run_graph(true, cold_region, 1, program, &storage, runtime, &context);
    assert(cold.ran && cold.result.f == wasm_smoke_expected(cold_region));
    assert(cold.wasm_executions == 1);
    printf(
        "browser worker Wasm first miss ops=%d inputs=%d wasm_calls=%u "
        "worker_ns=%.1f roundtrip_ns=%.1f\n",
        wasm_smoke_operations[cold_region],
        wasm_smoke_inputs[cold_region],
        cold.wasm_executions,
        cold.worker_ms * 1000000.0,
        cold.caller_ms * 1000000.0
    );

    for (int region = 0; region < WASM_SMOKE_REGIONS; region++) {
        double wasm_worker[WASM_SMOKE_WORKER_TRIALS];
        double bytecode_worker[WASM_SMOKE_WORKER_TRIALS];
        double wasm_roundtrip[WASM_SMOKE_WORKER_TRIALS];
        double bytecode_roundtrip[WASM_SMOKE_WORKER_TRIALS];
        for (int trial = 0; trial < WASM_SMOKE_WORKER_TRIALS; trial++) {
            for (int pass = 0; pass < 2; pass++) {
                bool wasm = (trial + pass) % 2 == 0;
                wasm_worker_result result = wasm_smoke_worker_run_graph(
                    wasm,
                    region,
                    WASM_SMOKE_BENCH_ITERATIONS,
                    program,
                    &storage,
                    runtime,
                    &context
                );
                assert(result.ran && result.result.f == wasm_smoke_expected(region));
                assert(
                    result.wasm_executions ==
                    (wasm && wasm_smoke_region_eligible(region) ? WASM_SMOKE_BENCH_ITERATIONS
                                                                : 0)
                );
                double worker_ns = result.worker_ms * 1000000.0 /
                                   WASM_SMOKE_BENCH_ITERATIONS;
                double roundtrip_ns = result.caller_ms * 1000000.0 /
                                      WASM_SMOKE_BENCH_ITERATIONS;
                if (wasm) {
                    wasm_worker[trial] = worker_ns;
                    wasm_roundtrip[trial] = roundtrip_ns;
                } else {
                    bytecode_worker[trial] = worker_ns;
                    bytecode_roundtrip[trial] = roundtrip_ns;
                }
            }
            assert(
                wasm_worker[trial] > 0.0 && bytecode_worker[trial] > 0.0 &&
                wasm_roundtrip[trial] > 0.0 && bytecode_roundtrip[trial] > 0.0
            );
            printf(
                "browser worker sample region=%d trial=%d ops=%d inputs=%d "
                "worker_ns wasm/bytecode=%.1f/%.1f "
                "batch_roundtrip_ns=%.1f/%.1f\n",
                region,
                trial,
                wasm_smoke_operations[region],
                wasm_smoke_inputs[region],
                wasm_worker[trial],
                bytecode_worker[trial],
                wasm_roundtrip[trial],
                bytecode_roundtrip[trial]
            );
        }
        printf(
            "browser worker region ops=%d inputs=%d case=%s path=%s worker_ns "
            "wasm/bytecode=%.1f/%.1f batch_roundtrip_ns=%.1f/%.1f\n",
            wasm_smoke_operations[region],
            wasm_smoke_inputs[region],
            wasm_smoke_rounding[region] ? "f32-rounding" : "sum",
            wasm_smoke_region_eligible(region) ? "wasm" : "fallback",
            wasm_smoke_median(wasm_worker, WASM_SMOKE_WORKER_TRIALS),
            wasm_smoke_median(bytecode_worker, WASM_SMOKE_WORKER_TRIALS),
            wasm_smoke_median(wasm_roundtrip, WASM_SMOKE_WORKER_TRIALS),
            wasm_smoke_median(bytecode_roundtrip, WASM_SMOKE_WORKER_TRIALS)
        );
    }

    double wasm_handoff[WASM_SMOKE_WORKER_TRIALS];
    double bytecode_handoff[WASM_SMOKE_WORKER_TRIALS];
    double wasm_direct[WASM_SMOKE_WORKER_TRIALS];
    double bytecode_direct[WASM_SMOKE_WORKER_TRIALS];
    for (int trial = 0; trial < WASM_SMOKE_WORKER_TRIALS; trial++) {
        for (int pass = 0; pass < 2; pass++) {
            bool wasm = (trial + pass) % 2 == 0;
            double caller_ms = 0.0;
            double worker_ms = 0.0;
            for (int i = 0; i < WASM_SMOKE_WORKER_HANDOFFS; i++) {
                wasm_worker_result
                    result = wasm_smoke_worker_run_graph(wasm, 1, 1, program, &storage, runtime, &context);
                assert(result.ran && result.result.f == wasm_smoke_expected(1));
                assert(result.wasm_executions == (wasm ? 1u : 0u));
                caller_ms += result.caller_ms;
                worker_ms += result.worker_ms;
            }
            double roundtrip_ns = caller_ms * 1000000.0 / WASM_SMOKE_WORKER_HANDOFFS;
            double direct_ns = worker_ms * 1000000.0 / WASM_SMOKE_WORKER_HANDOFFS;
            if (wasm) {
                wasm_handoff[trial] = roundtrip_ns;
                wasm_direct[trial] = direct_ns;
            } else {
                bytecode_handoff[trial] = roundtrip_ns;
                bytecode_direct[trial] = direct_ns;
            }
        }
        printf(
            "browser worker one-job sample trial=%d ops=16 inputs=1 "
            "roundtrip_ns wasm/bytecode=%.1f/%.1f worker_ns=%.1f/%.1f\n",
            trial,
            wasm_handoff[trial],
            bytecode_handoff[trial],
            wasm_direct[trial],
            bytecode_direct[trial]
        );
    }
    printf(
        "browser worker one-job handoff ops=16 inputs=1 roundtrip_ns "
        "wasm/bytecode=%.1f/%.1f worker_ns=%.1f/%.1f\n",
        wasm_smoke_median(wasm_handoff, WASM_SMOKE_WORKER_TRIALS),
        wasm_smoke_median(bytecode_handoff, WASM_SMOKE_WORKER_TRIALS),
        wasm_smoke_median(wasm_direct, WASM_SMOKE_WORKER_TRIALS),
        wasm_smoke_median(bytecode_direct, WASM_SMOKE_WORKER_TRIALS)
    );
    runtime_store_free(runtime);
    arena_free(&storage);
}

static void wasm_smoke_pthread_workers(const behavior_graph_program *program)
{
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, wasm_smoke_worker, NULL));

    behavior_expression_wasm_region *regions = NULL;
    behavior_expression_wasm *add = wasm_smoke_worker_module(BEHAVIOR_WASM_ADD, &regions);
    afree(regions);
    pin_value slots[WASM_WORKER_SLOT_COUNT] = { 0 };
    slots[WASM_WORKER_OPERATIONS] = (pin_value){ .type = PIN_F32, .f = 16777216.0f };
    slots[WASM_WORKER_OPERATIONS + 1] = (pin_value){ .type = PIN_F32, .f = 1.0f };
    wasm_worker_result result = wasm_smoke_worker_run(add, slots);
    assert(result.ran && result.result.f == 16777216.0f && result.cache_size == 1);
    result = wasm_smoke_worker_run(add, slots);
    assert(result.ran && result.result.f == 16777216.0f && result.cache_size == 1);

    behavior_expression_wasm_free(add);
    behavior_expression_wasm_region *subtract_regions = NULL;
    behavior_expression_wasm *subtract = wasm_smoke_worker_module(BEHAVIOR_WASM_SUB, &subtract_regions);
    afree(subtract_regions);
    slots[WASM_WORKER_OPERATIONS] = (pin_value){ .type = PIN_F32, .f = 32.0f };
    result = wasm_smoke_worker_run(subtract, slots);
    assert(result.ran && result.result.f == 16.0f && result.cache_size == 1);
    behavior_expression_wasm_free(subtract);

    behavior_expression_wasm *modules[WASM_WORKER_CACHE_TEST_MODULES];
    slots[WASM_WORKER_OPERATIONS] = (pin_value){ .type = PIN_F32, .f = 0.0f };
    slots[WASM_WORKER_OPERATIONS + 1] = (pin_value){ .type = PIN_F32, .f = 0.0f };
    for (int i = 0; i < WASM_WORKER_CACHE_TEST_MODULES; i++) {
        behavior_expression_wasm_region *module_regions = NULL;
        modules[i] = wasm_smoke_worker_module(BEHAVIOR_WASM_ADD, &module_regions);
        afree(module_regions);
        result = wasm_smoke_worker_run(modules[i], slots);
        assert(result.ran && result.result.f == 0.0f);
        int expected_size = i + 1 < WASM_WORKER_CACHE_CAPACITY
                              ? i + 1
                              : WASM_WORKER_CACHE_CAPACITY;
        assert(result.cache_size == expected_size);
    }
    result = wasm_smoke_worker_run(modules[0], slots);
    assert(result.ran && result.cache_size == WASM_WORKER_CACHE_CAPACITY);
    for (int i = 0; i < WASM_WORKER_CACHE_TEST_MODULES; i++) {
        behavior_expression_wasm_free(modules[i]);
    }
    behavior_expression_wasm_region *last_regions = NULL;
    behavior_expression_wasm *last = wasm_smoke_worker_module(BEHAVIOR_WASM_SUB, &last_regions);
    afree(last_regions);
    slots[WASM_WORKER_OPERATIONS] = (pin_value){ .type = PIN_F32, .f = 32.0f };
    slots[WASM_WORKER_OPERATIONS + 1] = (pin_value){ .type = PIN_F32, .f = 1.0f };
    result = wasm_smoke_worker_run(last, slots);
    assert(result.ran && result.result.f == 16.0f && result.cache_size == 1);
    behavior_expression_wasm_free(last);

    wasm_smoke_worker_benchmark(program);

    pthread_mutex_lock(&wasm_worker_mutex);
    assert(wasm_worker_state == 0);
    wasm_worker_state = 3;
    pthread_cond_broadcast(&wasm_worker_condition);
    pthread_mutex_unlock(&wasm_worker_mutex);
    assert(!pthread_join(worker, NULL));
}
#endif

static void wasm_smoke_start()
{
    wasm_smoke_region_selection();
    wasm_smoke_linear_regression();
    behavior_expression_wasm_stats before;
    behavior_expression_wasm_get_stats(&before);
    wasm_smoke_asset = behavior_asset_new("Browser expression Wasm");
    graph *g = wasm_smoke_asset->graph;
    node *event = graph_add_node(g, &NODE_Event);
    for (int i = 0; i < WASM_SMOKE_MAX_INPUTS; i++) {
        char input_name[16];
        snprintf(input_name, sizeof input_name, "Input%d", i);
        behavior_variable *input = behavior_asset_add_variable(
            wasm_smoke_asset,
            input_name,
            PIN_F32,
            "float",
            PIN_CONTAINER_SINGLE,
            &(pin_value){ .type = PIN_F32, .f = wasm_smoke_input_value(i) },
            ""
        );
        wasm_smoke_input_ids[i] = input->id;
        wasm_smoke_input_gets[i] = behavior_asset_add_get_node(wasm_smoke_asset, wasm_smoke_input_ids[i]);
    }
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
    wasm_smoke_loop_set = behavior_asset_add_set_node(wasm_smoke_asset, wasm_smoke_loop_output_id);
    node *loop_value = wasm_smoke_expression(g, wasm_smoke_input_gets, wasm_smoke_operations[1], 1);
    assert(!graph_connect_data(g, loop_value, 0, wasm_smoke_loop_set, 1));
    assert(!graph_connect_exec(g, event, 0, loop, 0, 0));
    assert(!graph_connect_exec(g, loop, 0, wasm_smoke_loop_set, 0, 0));

    /* Put the F32 regression early so later large matrix candidates cannot
     * consume the graph-wide compile candidate budget before it is reached. */
    static const int output_order[WASM_SMOKE_REGIONS] = { 7, 0, 1, 2, 3, 4, 5, 6 };
    for (int order = 0; order < WASM_SMOKE_REGIONS; order++) {
        int i = output_order[order];
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
        if (i == 7) {
            node *value = wasm_smoke_rounding_expression(
                g,
                wasm_smoke_input_gets[0],
                wasm_smoke_operations[i]
            );
            assert(!graph_connect_data(g, value, 0, wasm_smoke_sets[i], 1));
        }
    }
    for (int i = 0; i < WASM_SMOKE_REGIONS - 1; i++) {
        node *value = wasm_smoke_expression(
            g,
            wasm_smoke_input_gets,
            wasm_smoke_operations[i],
            wasm_smoke_inputs[i]
        );
        assert(!graph_connect_data(g, value, 0, wasm_smoke_sets[i], 1));
    }
    for (int i = 0; i < WASM_SMOKE_REGIONS; i++) {
        assert(
            !graph_connect_exec(g, i ? wasm_smoke_sets[i - 1] : loop, i ? 0 : 2, wasm_smoke_sets[i], 0, 0)
        );
    }
    wasm_smoke_instance = behavior_instance_create(wasm_smoke_asset, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(wasm_smoke_instance, 0);
    const behavior_graph_program
        *program = behavior_program_graph(wasm_smoke_asset->program, g);
    assert(behavior_graph_program_wasm_expression_count(program) >= WASM_SMOKE_ELIGIBLE_REGIONS);
    behavior_expression_wasm_stats compiled;
    behavior_expression_wasm_get_stats(&compiled);
    assert(compiled.modules_requested == before.modules_requested + 2);
    /* The linear module contributes an implementation-dependent count. The
     * graph accessor supplies the contract for the graph module itself. */
    assert(
        compiled.expressions_eligible >=
        before.expressions_eligible +
            (unsigned)behavior_graph_program_wasm_expression_count(program)
    );
    wasm_smoke_dispatch();
    for (int i = 0; i < WASM_SMOKE_REGIONS; i++) {
        pin_value result;
        assert(
            behavior_instance_get_variable(wasm_smoke_instance, wasm_smoke_output_ids[i], &result) == 0
        );
        assert(result.f == wasm_smoke_expected(i));
    }
    behavior_expression_wasm_get_stats(&compiled);
    assert(compiled.bytecode_fallbacks > before.bytecode_fallbacks);
    assert(compiled.expressions_executed == before.expressions_executed);
#ifdef __EMSCRIPTEN_PTHREADS__
    wasm_smoke_pthread_workers(program);
    behavior_expression_wasm_get_stats(&compiled);
#endif
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
