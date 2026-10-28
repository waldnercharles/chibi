#include "ecs/ecs.h"
#include "ecs/ecs_internal.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module_effects.h"
#include "execution/module_internal.h"
#include "execution/prepared.h"
#include "execution/status.h"
#ifdef __EMSCRIPTEN__
#include "execution/wasm_jit.h"
#endif
#include "execution/storage.h"
#include "language/lower.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static execution_module_plan *plan;
static execution_module *module, *loaded;
static behavior_function_builder *builder;
static behavior_function *binding_oracle, *binding_direct, *binding_caller;
static script_syntax *binding_syntax;
static script_analysis *binding_analysis;
static unsigned char *bytes, *again;
static ecs_world *world;
static bool unsafe_link;
static char error[256];
static const type_field value_fields[] = {
    { .name = "value", .type = PIN_F64, .size = sizeof(double), .type_name = "f64" },
};
static const type_desc value_type = { .name = "query.scalar",
                                      .size = sizeof(double),
                                      .fields = value_fields,
                                      .field_count = 1 };
static const uint64_t reads[] = { 802 };
static const node_access read_access = { .reads = reads,
                                         .read_count = 1,
                                         .flags = NODE_EFFECT_PURE |
                                                  NODE_EFFECT_WORLD_READ };
static const node_access unsafe_access = { .flags = NODE_EFFECT_HOST_SINK };
static const char *read_value(void *service, const pin_value *arguments, pin_value *results)
{
    (void)service;
    (void)arguments;
    results[0].b = true;
    return NULL;
}
static const node_host_call read_call = { .service = "query.read", .invoke = read_value };
static void cleanup()
{
    if (builder) {
        behavior_function_builder_free(builder);
    }
    script_analysis_free(binding_analysis);
    script_syntax_free(binding_syntax);
    binding_analysis = NULL;
    binding_syntax = NULL;
    behavior_function_free(binding_caller);
    behavior_function_free(binding_oracle);
    behavior_function_free(binding_direct);
    binding_caller = binding_oracle = binding_direct = NULL;
    execution_module_plan_free(plan);
    execution_module_free(module);
    execution_module_free(loaded);
    if (world) {
        ecs_world_free(world);
    }
    free(bytes);
    free(again);
    builder = NULL;
    plan = NULL;
    module = loaded = NULL;
    world = NULL;
    bytes = again = NULL;
    unsafe_link = false;
}
static void emit(behavior_function_instruction instruction)
{
    behavior_function_builder_emit(builder, instruction);
}
static behavior_function *finish_function()
{
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    behavior_function *function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "query boundary function: %s\n", error);
    }
    behavior_function_builder_free(builder);
    builder = NULL;
    return function;
}
static void begin_function(int variables)
{
    builder = behavior_function_builder_new();
    pin_value boolean = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, NULL, 0, &boolean, 1, &boolean, variables);
}
static void return_true()
{
    /* Authored return updates private invocation control before its result. */
    int context = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, context, BEHAVIOR_ADDRESS);
    emit((
        behavior_function_instruction
    ){ .op = BF_CONTEXT, .type = BF_POINTER_TYPE, .output = context, .target = 1 });
    int slot = behavior_function_builder_slot(builder, PIN_BOOL);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_BOOL,
                                          .output = slot,
                                          .value = { .type = PIN_BOOL, .b = true } });
    emit((behavior_function_instruction){
        .op = BF_WRITE,
        .type = PIN_BOOL,
        .left = slot,
        .right = context,
        .offset = offsetof(behavior_function_context, exit_requested) });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_BOOL, .left = slot });
}
/* The removed value intrinsic's semantics, using the public context-host ABI.
 * Its two-value bank is a semantic oracle, not the old three-value timing path. */
static const char *query_binding_value(
    const behavior_function_context *context,
    void *service,
    const pin_value *args,
    pin_value *out
)
{
    (void)service;
    int variable = args[0].i;
    if (!context->query_variables || variable < 1 || variable > ECS_QUERY_VARS) {
        return "Query variable is not bound";
    }
    *out = (pin_value){ .type = PIN_HANDLE,
                        .u64 = context->query_variables[variable - 1] };
    return NULL;
}
static const node_host_call query_binding_call = { .context = query_binding_value };
static int binding_source()
{
    return behavior_function_builder_origin(
        builder,
        (behavior_function_source){ .graph_id = 8310,
                                    .call_id = 8311,
                                    .parent = -1,
                                    .asset_name = "QueryBinding.chibi",
                                    .start = 12,
                                    .end = 18,
                                    .line = 2,
                                    .column = 5 }
    );
}
static behavior_function *query_binding(int variable, bool direct, pin_type type)
{
    builder = behavior_function_builder_new();
    pin_value result = { .type = type };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    int value = behavior_function_builder_slot(builder, type);
    int source = binding_source();
    if (direct) {
        emit((behavior_function_instruction){ .op = BF_QUERY_VARIABLE,
                                              .type = type,
                                              .output = value,
                                              .target = variable,
                                              .source = source,
                                              .node_id = 8312,
                                              .pin = 7 });
    } else {
        pin_value argument = { .type = PIN_I32, .i = variable };
        int host = behavior_function_builder_host(builder, &query_binding_call, NULL, &argument, 1, &result, 1);
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = host,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                              .source = source,
                                              .node_id = 8312,
                                              .pin = 7 });
        int address = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
        behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, address, BEHAVIOR_ADDRESS);
        emit((behavior_function_instruction){
            .op = BF_HOST_SLOT,
            .type = BF_POINTER_TYPE,
            .output = address,
            .left = behavior_function_builder_host_results(builder, host) });
        emit((behavior_function_instruction){ .op = BF_LOAD,
                                              .type = PIN_HANDLE,
                                              .output = value,
                                              .left = address,
                                              .offset = offsetof(pin_value, d) });
    }
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = type, .left = value });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    behavior_function *function = behavior_function_builder_finish(builder, error, sizeof error);
    behavior_function_builder_free(builder);
    builder = NULL;
    return function;
}
static int inspect_binding(void *user, const behavior_function_point *point)
{
    (void)user;
    (void)point;
    return 0;
}
static bool check_query_binding(behavior_function *function, int variable)
{
    const uint64_t values[] = { UINT64_C(0x87654321abcdef01), 0, 37 };
    uint64_t tuple[ECS_QUERY_VARS];
    for (int i = 0; i < ECS_QUERY_VARS; i++) {
        tuple[i] = (uint64_t)i + 101;
    }
#ifdef __EMSCRIPTEN__
    execution_wasm_jit_stats before, after;
    execution_wasm_jit_get_stats(&before);
#endif
    REQUIRE(behavior_function_compile(function));
#ifndef __EMSCRIPTEN__
    REQUIRE(behavior_function_entry(function) != NULL);
#endif
    for (int backend = 0; backend < 2; backend++) {
        for (int inspected = 0; inspected < 2; inspected++) {
            /* Reuse one definition with distinct current tuples and contexts. */
            for (int sample = 0; sample < 4; sample++) {
                bool missing = sample == 2;
                uint64_t expected = values[sample == 3 ? 2 : sample];
                tuple[variable - 1] = expected;
                pin_value output = { .type = PIN_HANDLE, .u64 = UINT64_MAX };
                behavior_function_context context = {
                    .query_variables = missing ? NULL : tuple,
                    .results = &output,
                    .budget = 100,
                    .inspect = inspected ? inspect_binding : NULL,
                };
                int status = behavior_function_run(function, &context, backend == 1);
                if (status != (missing ? FLOW_ERR_INVALID_GRAPH : FLOW_OK)) {
                    fprintf(
                        stderr,
                        "query binding variable=%d backend=%d inspected=%d "
                        "sample=%d status=%d failure=%d message=%s\n",
                        variable,
                        backend,
                        inspected,
                        sample,
                        status,
                        context.failure,
                        context.error_message ? context.error_message : "none"
                    );
                }
                REQUIRE(status == (missing ? FLOW_ERR_INVALID_GRAPH : FLOW_OK));
                REQUIRE(output.type == PIN_HANDLE);
                REQUIRE(output.u64 == (missing ? UINT64_MAX : expected));
                if (!missing) {
                    REQUIRE(!context.error_message);
                    continue;
                }
                REQUIRE(
                    context.error_message &&
                    !strcmp(context.error_message, "Query variable is not bound")
                );
                REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
                REQUIRE(context.node_id == 8312 && context.error_pin == 7);
                REQUIRE(context.error_frame == 0 && context.error_source == 1);
                REQUIRE(context.error_stack == -1 && context.error_steps == 0);
                REQUIRE(acount(function->sources) == 2);
                const behavior_function_source
                    *source = &function->sources[context.error_source];
                REQUIRE(source->graph_id == 8310 && source->call_id == 8311);
                REQUIRE(source->start == 12 && source->end == 18);
                REQUIRE(source->line == 2 && source->column == 5);
                REQUIRE(!strcmp(source->asset_name, "QueryBinding.chibi"));
            }
            pin_value output = { .type = PIN_HANDLE, .u64 = UINT64_MAX };
            behavior_function_context context = { .results = &output,
                                                  .budget = 100,
                                                  .inspect = inspected ? inspect_binding
                                                                       : NULL };
            REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_ERR_INVALID_GRAPH);
            REQUIRE(
                context.error_message &&
                !strcmp(context.error_message, "Query variable is not bound")
            );
            context.query_variables = tuple;
            REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_OK);
            REQUIRE(!context.error_message && output.u64 == tuple[variable - 1]);
        }
    }
#ifdef __EMSCRIPTEN__
    execution_wasm_jit_get_stats(&after);
    REQUIRE(after.compilations > before.compilations);
    REQUIRE(after.executions - before.executions == 12);
    REQUIRE(after.compile_failures == before.compile_failures);
#else
    REQUIRE(function->artifact->inspection_code != NULL);
#endif
    return true;
}
TEST_CASE(query_binding_direct_matches_host_values_and_failures)
{
    for (int variable = 1; variable <= ECS_QUERY_VARS; variable++) {
        cleanup();
        binding_oracle = query_binding(variable, false, PIN_HANDLE);
        binding_direct = query_binding(variable, true, PIN_HANDLE);
        REQUIRE(binding_oracle && binding_direct);
        REQUIRE(acount(binding_oracle->host_calls) == 1);
        REQUIRE(acount(binding_oracle->data_values) == 2);
        REQUIRE(!binding_direct->host_calls && !binding_direct->data_values);
        REQUIRE(check_query_binding(binding_oracle, variable));
        REQUIRE(check_query_binding(binding_direct, variable));
    }
    return true;
}
TEST_CASE(query_binding_rejects_invalid_immediates_and_types)
{
    const int variables[] = { 0, -1, ECS_QUERY_VARS + 1, 1 };
    for (int i = 0; i < 4; i++) {
        binding_direct = query_binding(variables[i], true, i == 3 ? PIN_I32 : PIN_HANDLE);
        REQUIRE(!binding_direct);
        REQUIRE(strstr(error, "query variable"));
    }
    return true;
}
TEST_CASE(query_binding_unused_load_preserves_branches_and_budget)
{
    builder = behavior_function_builder_new();
    pin_value argument = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, &argument, 1, NULL, 0, NULL, 0);
    int condition = behavior_function_builder_slot(builder, PIN_BOOL);
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_BOOL, .output = condition, .value = argument });
    int branch = behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = condition }
    );
    int source = binding_source();
    emit((behavior_function_instruction){ .op = BF_CHECK, .source = source, .node_id = 8312 });
    int value = behavior_function_builder_slot(builder, PIN_HANDLE);
    emit((behavior_function_instruction){ .op = BF_QUERY_VARIABLE,
                                          .type = PIN_HANDLE,
                                          .output = value,
                                          .target = 1,
                                          .source = source,
                                          .node_id = 8312,
                                          .pin = 7 });
    int end = behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
    REQUIRE(behavior_function_builder_patch_target(builder, branch, end));
    binding_direct = finish_function();
    REQUIRE(binding_direct && behavior_function_compile(binding_direct));
    uint64_t tuple[ECS_QUERY_VARS] = { 0 };
    for (int backend = 0; backend < 2; backend++) {
        for (int inspected = 0; inspected < 2; inspected++) {
            for (int sample = 0; sample < 4; sample++) {
                argument.b = sample == 0;
                behavior_function_context context = {
                    .arguments = &argument,
                    .argument_count = 1,
                    .query_variables = sample == 3 ? tuple : NULL,
                    .budget = sample < 2 ? 0 : 1,
                    .inspect = inspected ? inspect_binding : NULL,
                };
                int status = behavior_function_run(binding_direct, &context, backend == 1);
                REQUIRE(
                    status == (sample == 1   ? FLOW_ERR_STEP_BUDGET
                               : sample == 2 ? FLOW_ERR_INVALID_GRAPH
                                             : FLOW_OK)
                );
                REQUIRE(context.steps == (sample < 2 ? 0u : 1u));
                if (sample == 1) {
                    REQUIRE(context.failure == BEHAVIOR_FUNCTION_BUDGET);
                }
                if (sample == 2) {
                    REQUIRE(context.failure == BEHAVIOR_FUNCTION_HOST_DATA);
                    REQUIRE(
                        context.error_message &&
                        !strcmp(context.error_message, "Query variable is not bound")
                    );
                    REQUIRE(context.node_id == 8312 && context.error_source == 1);
                    REQUIRE(context.error_pin == 7 && context.error_steps == 1);
                }
            }
        }
    }
    return true;
}
TEST_CASE(query_binding_preserves_live_integer_slots)
{
    enum
    {
        INPUTS = 12
    };
    builder = behavior_function_builder_new();
    pin_value arguments[INPUTS], results[INPUTS + 1];
    int slots[INPUTS];
    for (int i = 0; i < INPUTS; i++) {
        arguments[i] = (pin_value){ .type = PIN_U64,
                                    .u64 = UINT64_C(0xfedcba9800000000) + (unsigned)i };
        results[i] = (pin_value){ .type = PIN_U64 };
    }
    results[INPUTS] = (pin_value){ .type = PIN_HANDLE };
    behavior_function_builder_signature(builder, arguments, INPUTS, results, INPUTS + 1, NULL, 0);
    for (int i = 0; i < INPUTS; i++) {
        slots[i] = behavior_function_builder_slot(builder, PIN_U64);
        emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                              .type = PIN_U64,
                                              .output = slots[i],
                                              .left = i,
                                              .value = arguments[i] });
    }
    int value = behavior_function_builder_slot(builder, PIN_HANDLE);
    emit((behavior_function_instruction){ .op = BF_QUERY_VARIABLE,
                                          .type = PIN_HANDLE,
                                          .output = value,
                                          .target = ECS_QUERY_VARS });
    for (int i = 0; i < INPUTS; i++) {
        emit((
            behavior_function_instruction
        ){ .op = BF_RESULT, .type = PIN_U64, .left = slots[i], .target = i });
    }
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_HANDLE, .left = value, .target = INPUTS });
    binding_direct = finish_function();
    REQUIRE(binding_direct && behavior_function_compile(binding_direct));
    uint64_t tuple[ECS_QUERY_VARS] = { 0 };
    tuple[ECS_QUERY_VARS - 1] = UINT64_C(0x87654321abcdef01);
    for (int backend = 0; backend < 2; backend++) {
        for (int inspected = 0; inspected < 2; inspected++) {
            for (int i = 0; i <= INPUTS; i++) {
                results[i].u64 = 0;
            }
            behavior_function_context context = { .arguments = arguments,
                                                  .argument_count = INPUTS,
                                                  .results = results,
                                                  .query_variables = tuple,
                                                  .budget = 100,
                                                  .inspect = inspected ? inspect_binding
                                                                       : NULL };
            REQUIRE(behavior_function_run(binding_direct, &context, backend == 1) == FLOW_OK);
            for (int i = 0; i < INPUTS; i++) {
                REQUIRE(
                    results[i].type == PIN_U64 && results[i].u64 == arguments[i].u64
                );
            }
            REQUIRE(
                results[INPUTS].type == PIN_HANDLE &&
                results[INPUTS].u64 == tuple[ECS_QUERY_VARS - 1]
            );
        }
    }
    return true;
}
TEST_CASE(query_binding_prepared_roundtrip_preserves_errors_and_values)
{
    binding_direct = query_binding(ECS_QUERY_VARS, true, PIN_HANDLE);
    REQUIRE(binding_direct);
    plan = execution_module_plan_new();
    plan->valid = true;
    plan->id = 8320;
    apush(
        plan->functions,
        ((execution_module_function){ .id = 1,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = binding_direct })
    );
    binding_direct = NULL;
    plan->function_count = 1;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    size_t length = 0, repeated = 0;
    REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
    REQUIRE(length > 0 && length < 64 * 1024);
    loaded = execution_prepared_read(bytes, length, NULL, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(loaded);
    behavior_function *function = loaded->plan.functions[0].body;
    REQUIRE(!function->host_calls && !function->data_values);
    REQUIRE(check_query_binding(function, ECS_QUERY_VARS));
    REQUIRE(execution_prepared_write(loaded, &again, &repeated, error, sizeof error));
    REQUIRE(length == repeated && !memcmp(bytes, again, length));
    return true;
}
TEST_CASE(query_binding_predicate_requires_bound_variables_transitively)
{
    binding_direct = query_binding(ECS_QUERY_VARS, true, PIN_HANDLE);
    REQUIRE(binding_direct);
    unsigned bound = 1u | (1u << ECS_QUERY_VARS);
    REQUIRE(execution_query_predicate_valid(binding_direct, bound));
    REQUIRE(!execution_query_predicate_valid(binding_direct, 1u));
    begin_function(0);
    pin_value result = { .type = PIN_HANDLE };
    int call = behavior_function_builder_call(builder, binding_direct, NULL, 0, &result, 1);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    return_true();
    binding_caller = finish_function();
    REQUIRE(binding_caller);
    REQUIRE(execution_query_predicate_valid(binding_caller, bound));
    REQUIRE(!execution_query_predicate_valid(binding_caller, 1u));
    int target = -1;
    for (int i = 0; i < acount(binding_direct->instructions); i++) {
        if (binding_direct->instructions[i].op == BF_QUERY_VARIABLE) {
            target = i;
        }
    }
    REQUIRE(target >= 0);
    /* Private mutation simulates untrusted producers; no invalid body executes. */
    const int invalid[] = { 0, -1, ECS_QUERY_VARS + 1, 32 };
    for (int i = 0; i < 4; i++) {
        binding_direct->instructions[target].target = invalid[i];
        REQUIRE(!execution_query_predicate_valid(binding_direct, UINT_MAX));
        REQUIRE(!execution_query_predicate_valid(binding_caller, UINT_MAX));
    }
    binding_direct->instructions[target].target = ECS_QUERY_VARS;
    return true;
}
static double binding_seconds()
{
    struct timespec now;
    int status = clock_gettime(CLOCK_MONOTONIC, &now);
    assert(status == 0);
    (void)status;
    return now.tv_sec + now.tv_nsec * 1e-9;
}
TEST_CASE(query_binding_dispatch_has_bounded_measurements)
{
    enum
    {
        TRIALS = 5,
        READS = 512
    };
    binding_direct = query_binding(ECS_QUERY_VARS, true, PIN_HANDLE);
    REQUIRE(binding_direct && behavior_function_compile(binding_direct));
    uint64_t tuple[ECS_QUERY_VARS] = { 0 };
    tuple[ECS_QUERY_VARS - 1] = UINT64_C(0x87654321abcdef01);
    for (int backend = 0; backend < 2; backend++) {
        pin_value output = { .type = PIN_HANDLE };
        behavior_function_context warm = { .query_variables = tuple,
                                           .results = &output,
                                           .budget = 100 };
        REQUIRE(behavior_function_run(binding_direct, &warm, backend == 1) == FLOW_OK);
        for (int trial = 0; trial < TRIALS; trial++) {
            int status = FLOW_OK;
            xalloc_stats before, after;
            xalloc_stats_get(&before);
            uint64_t blocks = arena_block_allocations();
            double start = binding_seconds();
            for (int i = 0; i < READS; i++) {
                behavior_function_context context = { .query_variables = tuple,
                                                      .results = &output,
                                                      .budget = 100 };
                status |= behavior_function_run(binding_direct, &context, backend == 1);
            }
            double elapsed = binding_seconds() - start;
            xalloc_stats_get(&after);
            blocks = arena_block_allocations() - blocks;
            REQUIRE(status == FLOW_OK && output.u64 == tuple[ECS_QUERY_VARS - 1]);
            printf(
                "QUERY_BINDING path=direct_query_variable backend=%s trial=%d "
                "reads=%d us_per_read=%.6f host_calls=%d data_values=%d "
                "allocation_calls=%llu requested_bytes=%llu "
                "arena_blocks=%llu\n",
                backend ? "compiled" : "vm",
                trial,
                READS,
                elapsed * 1e6 / READS,
                acount(binding_direct->host_calls),
                acount(binding_direct->data_values),
                (unsigned long long)(after.calls - before.calls),
                (unsigned long long)(after.requested_bytes - before.requested_bytes),
                (unsigned long long)blocks
            );
        }
    }
    return true;
}
/* These tiny definitions bypass source diagnostics to exercise the module boundary. */
static behavior_function *function(int kind, int variables)
{
    begin_function(variables);
    if (kind == 1) {
        pin_value boolean = { .type = PIN_BOOL };
        int host = behavior_function_builder_host(builder, &read_call, &read_access, NULL, 0, &boolean, 1);
        behavior_function_builder_host_identity(builder, host, 9001, 1);
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                              .target = host });
    } else if (kind == 2) {
        int slot = behavior_function_builder_slot(builder, PIN_I32);
        emit((behavior_function_instruction){ .op = BF_RANDOM,
                                              .type = PIN_I32,
                                              .output = slot });
    } else if (kind == 3) {
        int slot = behavior_function_builder_slot(builder, PIN_BOOL);
        emit((
            behavior_function_instruction
        ){ .op = BF_VARIABLE, .type = PIN_BOOL, .output = slot, .left = 0 });
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = PIN_BOOL,
                                              .left = slot });
        return finish_function();
    } else if (kind == 4) {
        pin_value arguments[] = { { .type = BF_POINTER_TYPE },
                                  { .type = PIN_HANDLE },
                                  { .type = PIN_U64, .u64 = 801 } };
        pin_value result = { .type = PIN_BOOL };
        int host = behavior_function_builder_host(
            builder,
            &EXECUTION_component_has_call,
            NULL,
            arguments,
            3,
            &result,
            1
        );
        uint64_t id = execution_intrinsic_id(&EXECUTION_component_has_call);
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id));
        behavior_function_builder_kind(
            builder,
            BEHAVIOR_BANK_DATA,
            behavior_function_builder_host_first(builder, host),
            BEHAVIOR_ADDRESS
        );
        int context = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
        int address = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
        int offset = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
        int alias = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
        int recovered = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
        behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, context, BEHAVIOR_ADDRESS);
        behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, address, BEHAVIOR_ADDRESS);
        behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, alias, BEHAVIOR_ADDRESS);
        behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, recovered, BEHAVIOR_ADDRESS);
        emit((behavior_function_instruction){ .op = BF_CONTEXT,
                                              .type = BF_POINTER_TYPE,
                                              .output = context,
                                              .target = 1 });
        emit((behavior_function_instruction){
            .op = BF_HOST_SLOT,
            .type = BF_POINTER_TYPE,
            .output = address,
            .left = behavior_function_builder_host_first(builder, host) });
        emit((behavior_function_instruction){ .op = BF_WRITE,
                                              .type = BF_POINTER_TYPE,
                                              .left = context,
                                              .right = address,
                                              .offset = offsetof(pin_value, d) });
        emit((behavior_function_instruction){
            .op = BF_CONSTANT,
            .type = BF_POINTER_TYPE,
            .output = offset,
            .value = { .type = BF_POINTER_TYPE, .u64 = offsetof(pin_value, d) } });
        emit((behavior_function_instruction){ .op = BF_ADD,
                                              .type = BF_POINTER_TYPE,
                                              .output = alias,
                                              .left = address,
                                              .right = offset });
        emit((behavior_function_instruction){ .op = BF_LOAD,
                                              .type = BF_POINTER_TYPE,
                                              .output = recovered,
                                              .left = alias });
    }
    return_true();
    return finish_function();
}
static bool prepare()
{
    plan = execution_module_plan_new();
    plan->valid = true;
    plan->id = 8000;
    behavior_function *body = function(0, 0);
    REQUIRE(body);
    apush(
        plan->functions,
        ((execution_module_function){ .id = 1, .kind = EXECUTION_FUNCTION_SYSTEM, .body = body })
    );
    plan->function_count = 1;
    behavior_function *predicate = function(1, 0);
    REQUIRE(predicate);
    apush(
        plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_GENERATOR,
                                   .component = 901,
                                   .target_variable = 1 })
    );
    apush(
        plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_CHANGED,
                                   .component = 801,
                                   .source_variable = 1,
                                   .source_relationship = 902 })
    );
    apush(plan->queries, ((execution_module_query){ .kind = EXECUTION_QUERY_VALUE, .predicate = predicate }));
    int *children = malloc(3 * sizeof *children);
    REQUIRE(children);
    children[0] = 0;
    children[1] = 1;
    children[2] = 2;
    apush(
        plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_ALL,
                                   .children = children,
                                   .child_count = 3 })
    );
    plan->query_count = acount(plan->queries);
    plan->query = 3;
    plan->query_variable_count = 1;
    return true;
}
static bool resolve_host(void *user, uint64_t id, execution_prepared_host *out)
{
    (void)user;
    if (id != 9001) {
        return false;
    }
    *out = (execution_prepared_host){ .call = &read_call,
                                      .access = unsafe_link ? &unsafe_access : &read_access,
                                      .abi = 1 };
    return true;
}
static const execution_prepared_linker linker = { .host = resolve_host };
static bool read_dependency(execution_effect_summary effects, uint64_t id)
{
    for (int i = 0; i < effects.read_count; i++) {
        if (effects.reads[i] == id) {
            return true;
        }
    }
    return false;
}
TEST_CASE(query_predicates_roundtrip_with_services_effects_and_preflight)
{
    REQUIRE(prepare());
    /* A mandatory nested conjunction must retain its generator before the
     * downstream variable-dependent leaf, including after serialization. */
    plan->queries[3].children[1] = 2;
    plan->queries[3].child_count = 2;
    int *children = malloc(2 * sizeof *children);
    REQUIRE(children);
    children[0] = 3;
    children[1] = 1;
    apush(
        plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_ALL,
                                   .children = children,
                                   .child_count = 2 })
    );
    plan->query = plan->query_count++;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    execution_effect_summary effects = execution_module_effects(module);
    REQUIRE(!effects.parallel_safe);
    REQUIRE(effects.declared && !effects.structural && !effects.host_sink);
    REQUIRE(read_dependency(effects, 801) && read_dependency(effects, 802));
    REQUIRE(read_dependency(effects, 901) && read_dependency(effects, 902));
    int count = 0;
    const char *const *services = execution_module_services(module, &count);
    REQUIRE(count == 2);
    REQUIRE(!strcmp(services[0], "query.read") && !strcmp(services[1], "ecs.world"));
    REQUIRE(execution_module_costs(module).possible != 0);
    REQUIRE(
        execution_module_storage_bytes(module) >
        behavior_function_storage_bytes(module->plan.queries[2].predicate)
    );
    size_t length = 0, repeated = 0;
    REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
    loaded = execution_prepared_read(bytes, length, &linker, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!loaded) {
        fprintf(stderr, "query boundary roundtrip: %s\n", error);
    }
    REQUIRE(loaded);
    REQUIRE(loaded->plan.query_variable_count == 1 && loaded->plan.queries[2].predicate);
    REQUIRE(
        loaded->plan.queries[0].target_variable == 1 &&
        loaded->plan.queries[1].source_variable == 1
    );
    REQUIRE(execution_prepared_write(loaded, &again, &repeated, error, sizeof error));
    REQUIRE(length == repeated && !memcmp(bytes, again, length));
    world = ecs_world_new();
    const ecs_relationship_desc relationship = { .id = 901,
                                                 .key = "query.targets",
                                                 .name = "Targets" };
    const ecs_relationship_desc source = { .id = 902,
                                           .key = "query.source",
                                           .name = "Source",
                                           .traits = ECS_REL_EXCLUSIVE };
    REQUIRE(ecs_world_register_relationship(world, &relationship, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_relationship(world, &source, NULL) == ECS_OK);
    ecs_component_desc component = { .id = 801,
                                     .type = &value_type,
                                     .key = "query.position",
                                     .name = "Position",
                                     .element_size = sizeof(double),
                                     .layout_hash = 1 };
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    REQUIRE(!execution_module_components_valid(loaded, world));
    component.id = 802;
    component.key = "query.health";
    component.name = "Health";
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    REQUIRE(execution_module_components_valid(loaded, world));
    cleanup();
    REQUIRE(prepare());
    children = malloc(2 * sizeof *children);
    REQUIRE(children);
    children[0] = 1;
    children[1] = 2;
    apush(
        plan->queries,
        ((execution_module_query){ .kind = EXECUTION_QUERY_ALL,
                                   .children = children,
                                   .child_count = 2 })
    );
    plan->query_filter = ++plan->query_count;
    plan->queries[3].children[1] = 4;
    plan->queries[3].child_count = 2;
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
    loaded = execution_prepared_read(bytes, length, &linker, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(loaded && loaded->plan.query_filter == 5);
    return true;
}
static behavior_function *binding_predicate(int variable)
{
    begin_function(0);
    int handle = behavior_function_builder_slot(builder, PIN_HANDLE);
    int zero = behavior_function_builder_slot(builder, PIN_HANDLE);
    int equal = behavior_function_builder_slot(builder, PIN_BOOL);
    emit((behavior_function_instruction){ .op = BF_QUERY_VARIABLE,
                                          .type = PIN_HANDLE,
                                          .output = handle,
                                          .target = variable });
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_HANDLE,
                                          .output = zero,
                                          .value = { .type = PIN_HANDLE } });
    emit((
        behavior_function_instruction
    ){ .op = BF_EQ, .type = PIN_HANDLE, .output = equal, .left = handle, .right = zero });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_BOOL, .left = equal });
    return finish_function();
}
TEST_CASE(query_binding_publication_and_prepared_reader_reject_forged_bound_masks)
{
    for (int variable = 2; variable >= 1; variable--) {
        cleanup();
        REQUIRE(prepare());
        behavior_function_free(plan->queries[2].predicate);
        plan->queries[2].predicate = binding_predicate(variable);
        REQUIRE(plan->queries[2].predicate);
        module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(variable == 1 ? module && !plan : !module && plan);
        if (variable == 2) {
            REQUIRE(strstr(error, "query"));
        }
    }
    size_t length = 0;
    REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
    REQUIRE(length > 0 && length < 64 * 1024);
    loaded = execution_prepared_read(bytes, length, NULL, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(loaded);
    execution_module_free(loaded);
    loaded = NULL;
    behavior_function *predicate = module->plan.queries[2].predicate;
    int target = -1;
    for (int i = 0; i < acount(predicate->instructions); i++) {
        if (predicate->instructions[i].op == BF_QUERY_VARIABLE) {
            target = i;
        }
    }
    REQUIRE(target >= 0);
    /* Target 2 is a valid instruction immediate, but only target 1 is bound. */
    predicate->instructions[target].target = 2;
    bool written = execution_prepared_write(module, &again, &length, error, sizeof error);
    predicate->instructions[target].target = 1;
    REQUIRE(written);
    loaded = execution_prepared_read(again, length, NULL, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(!loaded && strstr(error, "query"));
    return true;
}
static bool direct_binding_structure(const behavior_function *function, int *reads)
{
    REQUIRE(function);
    REQUIRE(!function->host_calls && !function->data_values);
    for (int i = 0; i < acount(function->instructions); i++) {
        const behavior_function_instruction *instruction = &function->instructions[i];
        REQUIRE(instruction->op != BF_HOST_CALL && instruction->op != BF_HOST_SLOT);
        if (instruction->op == BF_QUERY_VARIABLE) {
            REQUIRE(instruction->type == PIN_HANDLE && instruction->target == 1);
            (*reads)++;
        }
    }
    return true;
}
static bool binding_global(void *user, uint64_t id, behavior_function_instruction *out)
{
    (void)user;
    if (id != 30) {
        return false;
    }
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}
TEST_CASE(query_binding_authored_join_lowers_without_host_banks)
{
    /* Reuse the registered scalar Position fixture's real reflected layout. */
    const type_desc *position = type_catalog_find("Position");
    REQUIRE(position);
    const script_catalog_field field = { .id = 1011,
                                         .name = "x",
                                         .type = { .storage = PIN_F64 },
                                         .writable = true };
    const script_catalog_type type = { .id = 101,
                                       .name = "Position",
                                       .type = { .storage = PIN_STRUCT, .nominal = 101 },
                                       .fields = &field,
                                       .field_count = 1,
                                       .size = position->size,
                                       .layout = type_layout_hash(position),
                                       .component = true,
                                       .component_id = 501 };
    const script_catalog_relationship relationship = { .id = 601,
                                                       .name = "Targets",
                                                       .exclusive = true };
    const script_catalog_global owner = { .id = 30,
                                          .name = "self",
                                          .type = { .storage = PIN_HANDLE,
                                                    .nominal = 700 } };
    const script_catalog catalog = { .types = &type,
                                     .type_count = 1,
                                     .relationships = &relationship,
                                     .relationship_count = 1,
                                     .globals = &owner,
                                     .global_count = 1,
                                     .entity_type = 700 };
    binding_syntax = script_parse("Direct :: system( root:Position) join entity in self.Targets where entity != self { if entity == self {} }");
    binding_analysis = script_analyze(binding_syntax, &catalog);
    REQUIRE(binding_analysis && binding_analysis->valid);
    plan = script_lower(binding_analysis, &catalog, &(script_lower_bindings){ .global = binding_global });
    REQUIRE(plan && plan->valid);
    int body_reads = 0, predicate_reads = 0;
    for (int i = 0; i < plan->function_count; i++) {
        REQUIRE(direct_binding_structure(plan->functions[i].body, &body_reads));
    }
    for (int i = 0; i < plan->query_count; i++) {
        if (plan->queries[i].predicate) {
            REQUIRE(direct_binding_structure(plan->queries[i].predicate, &predicate_reads));
        }
    }
    REQUIRE(body_reads > 0 && predicate_reads > 0);
    REQUIRE(plan->query_variable_count == 1);
    return true;
}
TEST_CASE(query_binding_order_is_checked_before_publication)
{
    for (int bad = 0; bad < 11; bad++) {
        cleanup();
        REQUIRE(prepare());
        switch (bad) {
            case 0: plan->query_variable_count = 9; break;
            case 1: plan->queries[0].source_variable = 1; break;
            case 2:
                plan->query_variable_count = 2;
                plan->queries[0].target_variable = 2;
                break;
            case 3: plan->queries[3].kind = EXECUTION_QUERY_ANY; break;
            case 4:
                plan->queries[3].children[0] = 1;
                plan->queries[3].children[1] = 0;
                break;
            case 5: plan->queries[3].child_count = 2; break;
            case 6:
                plan->query_filter = 3;
                break; /* Changed outside marked where. */
            case 7:
                plan->query_filter = 4;
                break; /* Generator inside marked where. */
            case 8: plan->query_filter = 5; break; /* Out of bounds. */
            case 9: {
                int *child = malloc(sizeof *child);
                REQUIRE(child);
                *child = 2;
                apush(
                    plan->queries,
                    ((execution_module_query){ .kind = EXECUTION_QUERY_NOT,
                                               .children = child,
                                               .child_count = 1 })
                );
                plan->query_count++;
                plan->queries[1].kind = EXECUTION_QUERY_HAS;
                plan->queries[3].children[2] = 4;
                plan->query_filter = 3;
                break;
            }
            case 10: {
                int *children = realloc(plan->queries[3].children, 4 * sizeof *children);
                REQUIRE(children);
                plan->queries[3].children = children;
                children[3] = 2;
                plan->queries[3].child_count = 4;
                plan->queries[1].kind = EXECUTION_QUERY_HAS;
                plan->query_filter = 3;
                break;
            }
        }
        module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(!module && plan);
        REQUIRE(strstr(error, "query"));
    }
    return true;
}
TEST_CASE(query_predicate_purity_is_rechecked_after_native_linking)
{
    REQUIRE(prepare());
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(module);
    size_t length = 0;
    REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
    unsafe_link = true;
    loaded = execution_prepared_read(bytes, length, &linker, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(!loaded);
    REQUIRE(strstr(error, "predicate"));
    return true;
}
TEST_CASE(query_predicate_purity_follows_each_outlined_callee)
{
    for (int bad = 0; bad < 3; bad++) {
        cleanup();
        plan = execution_module_plan_new();
        plan->valid = true;
        plan->id = 8100;
        int variables = bad == 2;
        if (variables) {
            apush(plan->states, ((execution_module_state){ .id = 1, .initial.type = PIN_BOOL }));
            plan->state_count = 1;
        }
        behavior_function *first = function(0, variables);
        REQUIRE(first);
        apush(
            plan->functions,
            ((execution_module_function){ .id = 1,
                                          .kind = EXECUTION_FUNCTION_AUTHORED,
                                          .body = first })
        );
        behavior_function *second = function(bad ? bad + 1 : 0, variables);
        REQUIRE(second);
        apush(
            plan->functions,
            ((execution_module_function){ .id = 2,
                                          .kind = EXECUTION_FUNCTION_AUTHORED,
                                          .body = second })
        );
        plan->function_count = 2;
        begin_function(variables);
        pin_value boolean = { .type = PIN_BOOL };
        int call = behavior_function_builder_call(builder, first, NULL, 0, &boolean, 1);
        emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
        call = behavior_function_builder_call(builder, second, NULL, 0, &boolean, 1);
        emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
        return_true();
        behavior_function *predicate = finish_function();
        REQUIRE(predicate);
        apush(
            plan->queries,
            ((execution_module_query){ .kind = EXECUTION_QUERY_VALUE, .predicate = predicate })
        );
        plan->query_count = 1;
        plan->query = 0;
        module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
        REQUIRE(bad ? !module && plan : module && !plan);
        if (bad) {
            REQUIRE(strstr(error, "predicate"));
        }
    }
    return true;
}
TEST_CASE(query_predicate_context_cannot_be_recovered_through_bank_aliases)
{
    REQUIRE(prepare());
    behavior_function_free(plan->queries[2].predicate);
    plan->queries[2].predicate = function(4, 0);
    REQUIRE(plan->queries[2].predicate);
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(!module && plan);
    REQUIRE(strstr(error, "predicate"));
    return true;
}
TEST_CASE(query_change_versions_saturate)
{
    world = ecs_world_new();
    const ecs_component_desc component = { .id = 801,
                                           .type = &value_type,
                                           .key = "query.position",
                                           .name = "Position",
                                           .element_size = sizeof(double),
                                           .layout_hash = 1 };
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    REQUIRE(ecs_world_record_add(world, entity, component.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_component_track_changes(world, component.id) == ECS_OK);
    /* Inject the boundary rather than attempting an unbounded write loop.
     * Public SDK consumer cases do not include private ECS headers. */
    find_component(world, component.id)->versions[entity.slot] = UINT64_MAX - 1;
    REQUIRE(ecs_component_modified(world, entity, component.id) == ECS_OK);
    REQUIRE(ecs_component_version(world, entity, component.id) == UINT64_MAX);
    REQUIRE(ecs_component_modified(world, entity, component.id) == ECS_OK);
    REQUIRE(ecs_component_version(world, entity, component.id) == UINT64_MAX);
    return true;
}
void test_language_query_boundary()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(query_binding_direct_matches_host_values_and_failures);
    RUN_TEST_CASE(query_binding_rejects_invalid_immediates_and_types);
    RUN_TEST_CASE(query_binding_unused_load_preserves_branches_and_budget);
    RUN_TEST_CASE(query_binding_preserves_live_integer_slots);
    RUN_TEST_CASE(query_binding_prepared_roundtrip_preserves_errors_and_values);
    RUN_TEST_CASE(query_binding_predicate_requires_bound_variables_transitively);
    RUN_TEST_CASE(query_change_versions_saturate);
    RUN_TEST_CASE(query_predicates_roundtrip_with_services_effects_and_preflight);
    RUN_TEST_CASE(query_binding_publication_and_prepared_reader_reject_forged_bound_masks);
    RUN_TEST_CASE(query_binding_authored_join_lowers_without_host_banks);
    RUN_TEST_CASE(query_binding_order_is_checked_before_publication);
    RUN_TEST_CASE(query_predicate_purity_is_rechecked_after_native_linking);
    RUN_TEST_CASE(query_predicate_purity_follows_each_outlined_callee);
    RUN_TEST_CASE(query_predicate_context_cannot_be_recovered_through_bank_aliases);
    pu_clear_setup();
}

void test_language_query_binding_benchmark()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(query_binding_dispatch_has_bounded_measurements);
    pu_clear_setup();
}
