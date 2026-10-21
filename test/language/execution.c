#include "backend.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/log.h"
#include "execution/module_effects.h"
#include "execution/prepared_internal.h"
#include "execution/scalar.h"
#include "execution/service.h"
#include "execution/status.h"
#include "graph/shader/shader_graph.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/union.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct
{
    int32_t value;
} fixture_box;
static const type_field box_field = { .name = "value",
                                      .type = PIN_I32,
                                      .size = sizeof(int32_t),
                                      .type_name = "int" };
static const type_desc box_type = { .name = "fixture.box",
                                    .size = sizeof(fixture_box),
                                    .fields = &box_field,
                                    .field_count = 1 };
static const type_desc *const types[] = { &box_type };
type_catalog_pack test_program_types();
type_catalog_pack test_reductions_types();
type_catalog_pack test_graph_model_types();

int type_catalog_pack_count()
{
    return 5;
}

type_catalog_pack type_catalog_pack_at(int index)
{
    assert(index >= 0 && index < 5);
    if (index == 4) {
        return test_reductions_types();
    }
    if (index == 1) {
        return test_program_types();
    }
    if (index == 2) {
        return test_graph_model_types();
    }
    if (index == 3) {
        return (type_catalog_pack){ .types = SHADER_PACK.types,
                                    .type_count = SHADER_PACK.type_count,
                                    .enums = SHADER_PACK.enums,
                                    .enum_count = SHADER_PACK.enum_count };
    }
    return (type_catalog_pack){ .types = types, .type_count = 1 };
}

static behavior_function_builder *builder;
static behavior_function *function, *callee;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage, managed;
static int host_calls;
static service_binding *log_services;
static execution_module_plan *prepared_plan;
static execution_module *prepared_module;
static unsigned char *prepared_bytes, *prepared_mutation;

static void release_invocation()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
}

static void cleanup()
{
    release_invocation();
    execution_module_plan_free(prepared_plan);
    execution_module_free(prepared_module);
    free(prepared_bytes);
    afree(prepared_mutation);
    prepared_plan = NULL;
    prepared_module = NULL;
    prepared_bytes = prepared_mutation = NULL;
    arena_free(&managed);
    afree(log_services);
    if (builder) {
        behavior_function_builder_free(builder);
    }
    behavior_function_free(function);
    behavior_function_free(callee);
    builder = NULL;
    function = callee = NULL;
    host_calls = 0;
}

static int emit(behavior_function_instruction instruction)
{
    return behavior_function_builder_emit(builder, instruction);
}

static int slot(pin_type type)
{
    return behavior_function_builder_slot(builder, type);
}

static int address_slot()
{
    int index = slot(BF_POINTER_TYPE);
    assert(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, index, BEHAVIOR_ADDRESS));
    return index;
}

static bool finish()
{
    char error[256];
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "execution builder: %s\n", error);
    }
    return function != NULL;
}

static int run(
    const pin_value *arguments,
    int count,
    pin_value *variables,
    pin_value *results,
    bool native,
    unsigned budget
)
{
    release_invocation();
    bool resumable = behavior_function_resumable(function);
    if (resumable) {
        behavior_function_activation_init(function, &activation);
    }
    context = (behavior_function_context){ .activation = resumable ? &activation : NULL,
                                           .arguments = arguments,
                                           .argument_count = count,
                                           .variables = variables,
                                           .results = results,
                                           .budget = budget };
    behavior_function_bind_storage(&context, &storage);
    assert(behavior_function_bind(function, &context, NULL, 0));
    return behavior_function_run(function, &context, native);
}

static const char *scale(void *, const pin_value *arguments, pin_value *results)
{
    host_calls++;
    results[0].d = arguments[0].d * 2;
    return NULL;
}

static int source_pauses, source_captures;
static int source_pause(void *, behavior_function_context *, const behavior_source_point *)
{
    source_pauses++;
    return 1;
}
static void source_capture(
    void *,
    behavior_function_context *,
    const behavior_source_watch *,
    const pin_value *value
)
{
    assert(value && value->i == 37);
    source_captures++;
}
TEST_CASE(execution_source_inspection_is_lazy_and_preserves_resume)
{
    builder = behavior_function_builder_new();
    pin_value result = { .type = PIN_I32 };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    int point = behavior_function_builder_source_point(
        builder,
        (behavior_source_point){ .occurrence = 1, .kind = BEHAVIOR_SOURCE_STATEMENT }
    );
    int watch = behavior_function_builder_source_watch(
        builder,
        (behavior_source_watch){
            .origin = { .occurrence = 2, .kind = BEHAVIOR_SOURCE_EXPRESSION },
            .initial = result }
    );
    int value = slot(PIN_I32);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_I32,
                                          .output = value,
                                          .value = { .type = PIN_I32, .i = 37 } });
    emit((behavior_function_instruction){ .op = BF_SOURCE_POINT, .left = point });
    emit((
        behavior_function_instruction
    ){ .op = BF_SOURCE_WATCH, .type = PIN_I32, .left = value, .target = watch });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = value });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    REQUIRE(!function->artifact->inspection_code);
    int observers = 0;
    for (int pc = 0; pc < acount(function->instructions); pc++) {
        behavior_function_op op = function->instructions[pc].op;
        if (op == BF_SOURCE_POINT || op == BF_SOURCE_WATCH) {
            observers++;
            REQUIRE(!behavior_function_runtime_keeps(function, pc));
        }
    }
    REQUIRE(observers == 2);
    execution_cost_summary costs = behavior_function_costs(function);
    REQUIRE(costs.complete && !costs.proven && !costs.possible);
    pin_value output = result;
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(run(NULL, 0, NULL, &output, backend == 1, 100) == FLOW_OK);
        REQUIRE(output.i == 37);
        REQUIRE(!function->artifact->inspection_code);
    }
    for (int backend = 0; backend < 2; backend++) {
        source_pauses = source_captures = 0;
        context.source_pause = source_pause;
        context.source_capture = source_capture;
        REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_PAUSED_RESULT);
        REQUIRE(source_pauses == 1 && source_captures == 0);
        REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_OK);
        REQUIRE(source_pauses == 1 && source_captures == 1);
    }
#ifndef __EMSCRIPTEN__
    void *inspection = function->artifact->inspection_code;
    REQUIRE(inspection);
    REQUIRE(behavior_function_compile_inspection(function));
    REQUIRE(function->artifact->inspection_code == inspection);
#endif
    /* Detaching after a pause preserves the canonical continuation's state. */
    REQUIRE(behavior_function_run(function, &context, true) == FLOW_PAUSED_RESULT);
    context.source_pause = NULL;
    context.source_capture = NULL;
    REQUIRE(behavior_function_run(function, &context, true) == FLOW_OK);
    REQUIRE(source_captures == 1);
    return true;
}

TEST_CASE(execution_branches_state_and_host_calls)
{
    builder = behavior_function_builder_new();
    pin_value argument = { .type = PIN_F64 }, result = { .type = PIN_F64 };
    pin_value initial = { .type = PIN_F64, .d = 10 };
    behavior_function_builder_signature(builder, &argument, 1, &result, 1, &initial, 1);
    const node_host_call host = { .invoke = scale };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int capability = behavior_function_builder_host(builder, &host, &access, &argument, 1, &result, 1);
    int input = slot(PIN_F64), state = slot(PIN_F64), zero = slot(PIN_F64);
    int negative = slot(PIN_BOOL), sum = slot(PIN_F64);
    int pointer = address_slot(), scaled = slot(PIN_F64);
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_F64, .output = input, .value = argument });
    emit((behavior_function_instruction){ .op = BF_VARIABLE,
                                          .type = PIN_F64,
                                          .output = state });
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_F64,
                                          .output = zero,
                                          .value = { .type = PIN_F64 } });
    emit((
        behavior_function_instruction
    ){ .op = BF_LT, .type = PIN_F64, .output = negative, .left = input, .right = zero });
    int branch = emit((behavior_function_instruction){ .op = BF_BRANCH,
                                                       .left = negative });
    emit((
        behavior_function_instruction
    ){ .op = BF_ADD, .type = PIN_F64, .output = sum, .left = state, .right = input });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_host_first(builder, capability) });
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = PIN_F64,
                                          .left = sum,
                                          .right = pointer,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = capability,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_host_results(builder, capability) });
    emit((behavior_function_instruction){ .op = BF_READ,
                                          .type = PIN_F64,
                                          .output = scaled,
                                          .left = pointer,
                                          .offset = offsetof(pin_value, d) });
    emit((behavior_function_instruction){ .op = BF_STORE, .type = PIN_F64, .left = sum });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = scaled });
    int jump = emit((behavior_function_instruction){ .op = BF_JUMP });
    int unchanged = emit((behavior_function_instruction){ .op = BF_RESULT,
                                                          .type = PIN_F64,
                                                          .left = state });
    int end = emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(behavior_function_builder_patch_target(builder, branch, unchanged));
    REQUIRE(behavior_function_builder_patch_target(builder, jump, end));
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
#ifndef __EMSCRIPTEN__
    REQUIRE(behavior_function_entry(function) != NULL);
#endif
    for (int backend = 0; backend < 2; backend++) {
        pin_value variable = initial, output = result;
        argument.d = 3;
        host_calls = 0;
        REQUIRE(run(&argument, 1, &variable, &output, backend == 1, 100) == FLOW_OK);
        REQUIRE(variable.d == 13 && output.d == 26 && host_calls == 1);
        argument.d = -4;
        REQUIRE(run(&argument, 1, &variable, &output, backend == 1, 100) == FLOW_OK);
        if (variable.d != 13 || output.d != 13 || host_calls != 1) {
            fprintf(
                stderr,
                "branch backend=%s state=%g result=%g host_calls=%d\n",
                backend ? "native" : "VM",
                variable.d,
                output.d,
                host_calls
            );
        }
        REQUIRE(variable.d == 13 && output.d == 13 && host_calls == 1);
        argument.d = 2;
        REQUIRE(run(&argument, 1, &variable, &output, backend == 1, 100) == FLOW_OK);
        REQUIRE(variable.d == 15 && output.d == 30 && host_calls == 2);
    }
    return true;
}

TEST_CASE(execution_finished_definition_owns_producer_strings)
{
    char text[] = "producer text", type_name[] = "fixture.text",
         origin[] = "fixture.chibi";
    builder = behavior_function_builder_new();
    pin_value initial = { .type = PIN_STR, .str = text, .type_name = type_name };
    behavior_function_builder_signature(builder, NULL, 0, &initial, 1, NULL, 0);
    int source = behavior_function_builder_origin(
        builder,
        (behavior_function_source){ .asset_name = origin, .parent = -1, .line = 3, .column = 2 }
    );
    int output = slot(PIN_STR);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_STR,
                                          .output = output,
                                          .value = initial,
                                          .source = source });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_STR, .left = output });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    behavior_function_builder_free(builder);
    builder = NULL;
    memset(text, 'X', sizeof text - 1);
    memset(type_name, 'X', sizeof type_name - 1);
    memset(origin, 'X', sizeof origin - 1);
    int count = 0;
    const pin_value *defaults = behavior_function_results(function, &count);
    REQUIRE(count == 1 && defaults[0].type == PIN_STR);
    REQUIRE(defaults[0].str != text && !strcmp(defaults[0].str, "producer text"));
    REQUIRE(defaults[0].type_name != type_name && !strcmp(defaults[0].type_name, "fixture.text"));
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        pin_value result = defaults[0];
        REQUIRE(run(NULL, 0, NULL, &result, backend == 1, 100) == FLOW_OK);
        REQUIRE(!strcmp(result.str, "producer text"));
    }
    return true;
}

TEST_CASE(execution_verifier_rejects_invalid_lowering)
{
    const behavior_function_instruction invalid[] = {
        { .op = BF_CONSTANT, .type = PIN_I32, .output = 99, .value = { .type = PIN_I32 } },
        { .op = BF_ADD, .type = PIN_I32, .left = 99 },
        { .op = BF_BRANCH, .left = 1, .target = 99 },
        { .op = BF_HOST_CALL, .left = BEHAVIOR_FUNCTION_HOST_DATA, .target = 0 },
        { .op = BF_CONSTANT, .type = PIN_I32, .value = { .type = PIN_BOOL } },
        { .op = BF_ADD, .type = PIN_I32, .left = 1 },
        { .op = BF_CONSTANT, .type = PIN_I32, .output = 1, .value = { .type = PIN_I32 } },
        { .op = BF_STORE, .type = PIN_I32, .target = 0 },
        { .op = BF_ARGUMENT, .type = PIN_I32, .left = 0 },
    };
    for (int i = 0; i < 9; i++) {
        cleanup();
        builder = behavior_function_builder_new();
        slot(PIN_I32);
        slot(PIN_BOOL);
        emit(invalid[i]);
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        REQUIRE(function == NULL);
        REQUIRE(error[0] != '\0');
    }
    cleanup();
    builder = behavior_function_builder_new();
    char error[256];
    REQUIRE(behavior_function_builder_finish(builder, error, sizeof error) == NULL);
    REQUIRE(strstr(error, "END") != NULL);
    return true;
}

TEST_CASE(execution_budget_limits_checked_back_edges)
{
    builder = behavior_function_builder_new();
    int check = emit((behavior_function_instruction){ .op = BF_CHECK, .node_id = 17 });
    emit((behavior_function_instruction){ .op = BF_JUMP, .target = check });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(run(NULL, 0, NULL, NULL, backend == 1, 3) == FLOW_ERR_STEP_BUDGET);
        REQUIRE(context.failure == BEHAVIOR_FUNCTION_BUDGET);
        REQUIRE(context.node_id == 17 && context.error_steps == 3);
    }
    return true;
}

TEST_CASE(execution_linked_function_returns_checked_value)
{
    pin_value number = { .type = PIN_I32 };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &number, 1, &number, 1, NULL, 0);
    int input = slot(PIN_I32), two = slot(PIN_I32), product = slot(PIN_I32);
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_I32, .output = input, .value = number });
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_I32,
                                          .output = two,
                                          .value = { .type = PIN_I32, .i = 2 } });
    emit((
        behavior_function_instruction
    ){ .op = BF_MUL, .type = PIN_I32, .output = product, .left = input, .right = two });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = product });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    execution_cost_summary costs = behavior_function_costs(function);
    REQUIRE(costs.complete && !costs.proven && !costs.possible);
    callee = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, NULL, 0, &number, 1, NULL, 0);
    pin_value argument = { .type = PIN_I32, .i = 21 };
    int call = behavior_function_builder_call(builder, callee, &argument, 1, &number, 1);
    int pointer = address_slot(), output = slot(PIN_I32);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_call_results(builder, call) });
    emit((behavior_function_instruction){ .op = BF_READ,
                                          .type = PIN_I32,
                                          .output = output,
                                          .left = pointer,
                                          .offset = offsetof(pin_value, i) });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = output });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == EXECUTION_COST_ALLOCATE && !costs.possible);
    REQUIRE(behavior_function_compile(callee));
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        pin_value result = number;
        REQUIRE(run(NULL, 0, NULL, &result, backend == 1, 100) == FLOW_OK);
        REQUIRE(result.type == PIN_I32 && result.i == 42);
    }
    return true;
}

static int rebound_values[2];
static const char *rebound_host(void *service, const pin_value *, pin_value *results)
{
    if (results[0].type != PIN_I32 || results[0].i != 17) {
        return "helper retained its previous host result";
    }
    assert(host_calls < 2);
    rebound_values[host_calls++] = *(const int *)service;
    results[0].i = *(const int *)service;
    return NULL;
}

static const char *reused_box(void *service, const pin_value *, pin_value *results)
{
    fixture_box *box = results[0].ptr;
    if (!box || box->value) {
        return "host buffer retained its previous payload";
    }
    box->value = *(const int *)service;
    return NULL;
}

TEST_CASE(execution_reused_host_storage_clears_context_and_rebinds_fixed_buffers)
{
    builder = behavior_function_builder_new();
    const pin_value result = { .type = PIN_STRUCT, .type_name = "fixture.box" };
    const node_host_call host = { .service = "fixture.rebound", .invoke = reused_box };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int capability = behavior_function_builder_host(builder, &host, &access, NULL, 0, &result, 1);
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = capability,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    int service = 31;
    apush(log_services, ((service_binding){ "fixture.rebound", &service }));
    for (int backend = 0; backend < 2; backend++) {
        release_invocation();
        context.budget = 100;
        REQUIRE(behavior_function_bind(function, &context, log_services, 42));
        behavior_function_prepare_host(function, &context);
        behavior_data_storage *data = context.data;
        fixture_box *box = data->values[0].ptr;
        size_t bytes = behavior_function_host_storage_bytes(&context);
        uint64_t allocations = behavior_function_data_allocations();
        REQUIRE(bytes >= sizeof *data + sizeof *box);
        for (int invocation = 0; invocation < 2; invocation++) {
            service = invocation ? 73 : 31;
            REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_OK);
            REQUIRE(box->value == service);
            behavior_function_reset_host(&context);
            REQUIRE(context.data == data && context.owned_data);
            REQUIRE(
                !context.services && !context.owner && !context.source_function &&
                !context.arguments && !context.results && !context.value_storage
            );
            REQUIRE(data->values[0].ptr == box && !box->value && !data->calls[0].service);
            REQUIRE(behavior_function_host_storage_bytes(&context) == bytes);
            context.budget = 100;
            REQUIRE(behavior_function_bind(function, &context, log_services, 43));
            behavior_function_prepare_host(function, &context);
            REQUIRE(data->calls[0].service == &service);
        }
        REQUIRE(behavior_function_data_allocations() == allocations);
    }
    return true;
}

TEST_CASE(execution_reused_helper_resets_host_bank_and_rebinds_service)
{
    builder = behavior_function_builder_new();
    pin_value initial = { .type = PIN_I32, .i = 17 };
    const node_host_call host = { .service = "fixture.rebound", .invoke = rebound_host };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int capability = behavior_function_builder_host(builder, &host, &access, NULL, 0, &initial, 1);
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = capability,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    callee = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    int call = behavior_function_builder_call(builder, callee, NULL, 0, NULL, 0);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit((behavior_function_instruction){ .op = BF_SUSPEND });
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(callee));
    execution_cost_summary costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == EXECUTION_COST_ALLOCATE);
    REQUIRE(costs.possible == (EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY | EXECUTION_COST_ESCAPE));
    REQUIRE(costs.possible_sites[0].function == callee);
    REQUIRE(strstr(costs.possible_sites[0].reason, "Native call"));
    REQUIRE(behavior_function_compile(function));
    int first = 31, second = 73;
    apush(log_services, ((service_binding){ "fixture.rebound", &first }));
    for (int backend = 0; backend < 2; backend++) {
        release_invocation();
        behavior_function_activation_init(function, &activation);
        context = (behavior_function_context){ .activation = &activation,
                                               .budget = 100 };
        behavior_function_bind_storage(&context, &storage);
        log_services[0].value = &first;
        REQUIRE(behavior_function_bind(function, &context, log_services, 0));
        host_calls = 0;
        REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_SUSPENDED_RESULT);
        REQUIRE(host_calls == 1 && rebound_values[0] == first);
        /* The completed helper remains available while its parent is suspended.
         * Reusing it must initialize its host values and select current services. */
        log_services[0].value = &second;
        behavior_function_rebind_services(function, &context, log_services);
        REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_OK);
        REQUIRE(host_calls == 2 && rebound_values[1] == second);
    }
    return true;
}

TEST_CASE(execution_costs_verify_intrinsics_and_follow_recursive_calls)
{
    builder = behavior_function_builder_new();
    pin_value values[] = {
        { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY },
        { .type = PIN_I32, .i = 37 },
    };
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_construct_call,
        &EXECUTION_value_access,
        values,
        2,
        values,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_construct_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    execution_cost_summary costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == (EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY));
    REQUIRE(!costs.possible);
    REQUIRE(costs.proven_sites[1].capability == id);
    callee = function;
    function = NULL;
    behavior_function_builder_free(builder);
    builder = behavior_function_builder_new();
    int call = behavior_function_builder_call(builder, callee, NULL, 0, NULL, 0);
    emit((behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && !costs.possible);
    REQUIRE(costs.proven_sites[1].function == callee);
    /* Recursion revisits a definition without unbounded traversal. */
    function->calls[call].function = function;
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == EXECUTION_COST_ALLOCATE && !costs.possible);
    function->calls[call].function = callee;
    /* A matching stable ID alone must not lend a native callback the verified
     * intrinsic's cost contract. */
    const node_host_call changed = { .invoke = scale };
    callee->host_calls[host].call = &changed;
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == EXECUTION_COST_ALLOCATE);
    REQUIRE(costs.possible == (EXECUTION_COST_ALLOCATE | EXECUTION_COST_COPY | EXECUTION_COST_ESCAPE));
    callee->host_calls[host].call = &EXECUTION_construct_call;
    cleanup();
    builder = behavior_function_builder_new();
    pin_value tag = { .type = PIN_U64, .u64 = PIN_UNION_CANCELLED_TAG };
    pin_value result = { .type = PIN_UNION };
    host = behavior_function_builder_host(
        builder,
        &EXECUTION_union_make_call,
        &EXECUTION_value_access,
        &tag,
        1,
        &result,
        1
    );
    id = execution_intrinsic_id(&EXECUTION_union_make_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && !costs.proven && !costs.possible);
    const node_host_call *patterns[] = { &EXECUTION_union_test_call,
                                         &EXECUTION_union_get_call };
    for (int j = 0; j < 2; j++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value args[] = { { .type = PIN_UNION },
                             tag,
                             j ? (pin_value){ .type = PIN_I32 } : tag,
                             { .type = PIN_BOOL, .b = true },
                             tag };
        pin_value output = { .type = j ? PIN_UNION : PIN_BOOL };
        host = behavior_function_builder_host(
            builder,
            patterns[j],
            &EXECUTION_value_access,
            args,
            j ? 5 : 3,
            &output,
            1
        );
        id = execution_intrinsic_id(patterns[j]);
        REQUIRE(
            behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
        );
        emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                              .target = host,
                                              .left = BEHAVIOR_FUNCTION_HOST_DATA });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        costs = behavior_function_costs(function);
        REQUIRE(costs.complete && !costs.proven && !costs.possible);
        execution_module_function entry = { .id = 1,
                                            .kind = EXECUTION_FUNCTION_SYSTEM,
                                            .body = function };
        execution_module module = { .plan = { .functions = &entry,
                                              .function_count = 1 } };
        REQUIRE(execution_module_build_effects(&module));
        execution_effect_summary effects = execution_module_effects(&module);
        REQUIRE(effects.declared && effects.parallel_safe && effects.deterministic);
        REQUIRE(!effects.structural && !effects.host_sink && !effects.world_read);
        REQUIRE(!effects.read_count && !effects.write_count && !module.effect_ids);
    }
    cleanup();
    builder = behavior_function_builder_new();
    pin_value empty = { .type = PIN_STRUCT, .type_name = "fixture.box" };
    host = behavior_function_builder_host(
        builder,
        &EXECUTION_construct_call,
        &EXECUTION_value_access,
        &empty,
        1,
        &empty,
        1
    );
    id = execution_intrinsic_id(&EXECUTION_construct_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    costs = behavior_function_costs(function);
    REQUIRE(costs.complete && costs.proven == EXECUTION_COST_ALLOCATE);
    REQUIRE(costs.possible == EXECUTION_COST_COPY);
    return true;
}

TEST_CASE(execution_verifier_rejects_invalid_saved_aggregate_roots)
{
    for (int invalid = 0; invalid < 7; invalid++) {
        cleanup();
        builder = behavior_function_builder_new();
        int pointer = address_slot();
        behavior_function_builder_root(
            builder,
            BEHAVIOR_BANK_SLOT,
            pointer,
            (pin_value){ .type = PIN_TASK, .container = PIN_CONTAINER_ARRAY }
        );
        behavior_function *draft = (behavior_function *)
            behavior_function_builder_definition(builder);
        behavior_value_root *root = &draft->value_roots[0];
        switch (invalid) {
            case 0: root->bank = BEHAVIOR_BANK_ARGUMENT; break;
            case 1: root->index = acount(draft->slots); break;
            case 2: root->index = -1; break;
            case 3: root->shape.container = PIN_CONTAINER_SINGLE; break;
            case 4: root->shape.ptr = (void *)(uintptr_t)1; break;
            case 5: draft->slot_kinds[pointer] = BEHAVIOR_VALUE; break;
            case 6: {
                behavior_value_root duplicate = *root;
                apush(draft->value_roots, duplicate);
                break;
            }
        }
        emit((behavior_function_instruction){ .op = BF_END });
        char error[192];
        REQUIRE(!behavior_function_builder_finish(builder, error, sizeof error));
        REQUIRE(error[0]);
    }
    return true;
}

TEST_CASE(execution_costs_include_managed_scalar_result_and_state_escape)
{
    const pin_type types[] = { PIN_I32, PIN_STR, PIN_NAME, PIN_UNION, PIN_TASK };
    for (int mode = 0; mode < 10; mode++) {
        int i = mode % 5;
        bool persistent = mode >= 5;
        cleanup();
        builder = behavior_function_builder_new();
        pin_value initial = { .type = types[i] };
        behavior_function_builder_signature(builder, &initial, 1, &initial, !persistent, &initial, persistent);
        int value = slot(types[i]);
        emit((
            behavior_function_instruction
        ){ .op = BF_ARGUMENT, .type = types[i], .value = initial, .output = value });
        emit((behavior_function_instruction){ .op = persistent ? BF_STORE : BF_RESULT,
                                              .type = types[i],
                                              .left = value });
        emit((behavior_function_instruction){ .op = BF_RETURN });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        execution_cost_summary costs = behavior_function_costs(function);
        REQUIRE(costs.complete && !costs.possible);
        REQUIRE(costs.proven == (i ? EXECUTION_COST_ESCAPE : 0));
    }
    return true;
}

typedef struct
{
    pin_value expected;
    int matches, others;
} visited_aggregate;
static void count_aggregate(void *user, const pin_value *value)
{
    visited_aggregate *seen = user;
    if (value->container != PIN_CONTAINER_ARRAY) {
        return;
    }
    if (value->type == seen->expected.type && value->ptr == seen->expected.ptr) {
        seen->matches++;
    } else {
        seen->others++;
    }
}
TEST_CASE(execution_saved_aggregate_roots_keep_only_live_shapes)
{
    for (int union_array = 0; union_array < 2; union_array++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_value shape = { .type = union_array ? PIN_UNION : PIN_TASK,
                            .container = PIN_CONTAINER_ARRAY };
        pin_value arguments[] = { shape, shape };
        pin_value results[] = { { .type = BF_POINTER_TYPE },
                                { .type = BF_POINTER_TYPE } };
        behavior_function_builder_signature(builder, arguments, 2, results, 2, NULL, 0);
        int input[2], locals[2];
        for (int i = 0; i < 2; i++) {
            input[i] = address_slot();
            locals[i] = behavior_function_builder_local(builder, results[i]);
            REQUIRE(
                behavior_function_builder_kind(builder, BEHAVIOR_BANK_LOCAL, locals[i], BEHAVIOR_ADDRESS)
            );
            REQUIRE(
                behavior_function_builder_kind(builder, BEHAVIOR_BANK_RESULT, i, BEHAVIOR_ADDRESS)
            );
            behavior_function_builder_root(builder, BEHAVIOR_BANK_SLOT, input[i], shape);
            behavior_function_builder_root(builder, BEHAVIOR_BANK_LOCAL, locals[i], shape);
            emit((behavior_function_instruction){ .op = BF_ARRAY,
                                                  .type = BF_POINTER_TYPE,
                                                  .target = BF_ARGUMENT,
                                                  .left = i,
                                                  .output = input[i],
                                                  .value = shape });
            emit((behavior_function_instruction){ .op = BF_SAVE,
                                                  .type = BF_POINTER_TYPE,
                                                  .target = locals[i],
                                                  .left = input[i] });
        }
        emit((behavior_function_instruction){ .op = BF_SUSPEND });
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = BF_POINTER_TYPE,
                                              .left = input[0] });
        int restored = address_slot();
        emit((behavior_function_instruction){ .op = BF_LOCAL,
                                              .type = BF_POINTER_TYPE,
                                              .left = locals[0],
                                              .output = restored });
        emit((
            behavior_function_instruction
        ){ .op = BF_RESULT, .type = BF_POINTER_TYPE, .left = restored, .target = 1 });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        REQUIRE(acount(function->data_values) == 0);
        REQUIRE(acount(function->suspensions) == 1);
        REQUIRE(function->suspensions[0].root_count == 2);
        REQUIRE(behavior_function_compile(function));
        for (int i = 0; i < 2; i++) {
            arguments[i].ptr = pin_array_allocate(&managed, shape.type, NULL, 1);
        }
        for (int backend = 0; backend < 2; backend++) {
            REQUIRE(run(arguments, 2, NULL, results, backend == 1, 100) == FLOW_SUSPENDED_RESULT);
            /* Remove the independent argument roots to exercise only saved
             * slot/local metadata, including rejection of the dead array. */
            context.arguments = NULL;
            context.argument_count = 0;
            visited_aggregate seen = { .expected = arguments[0] };
            behavior_function_visit_values(function, &context, count_aggregate, &seen);
            REQUIRE(seen.matches == 2 && seen.others == 0);
            context.arguments = arguments;
            context.argument_count = 2;
            REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_OK);
            REQUIRE(
                results[0].ptr == arguments[0].ptr &&
                results[1].ptr == arguments[0].ptr
            );
        }
    }
    return true;
}

TEST_CASE(execution_verifier_rejects_invalid_banks_and_metadata)
{
    const behavior_function_instruction invalid[] = {
        { .op = BF_STORE_VALUE },
        { .op = BF_STORE_HEADER, .left = 2, .right = 1 },
        { .op = BF_STORE_TYPE_NAME, .left = 2 },
        { .op = BF_RESULT_PRESENT, .left = 1 },
        { .op = BF_RESULT_TYPE_NAME, .left = 2 },
        { .op = BF_RESULT_ARRAY, .left = 2, .right = 2, .type = PIN_I32 },
        { .op = BF_STORAGE_NAME, .output = 2, .type = BF_POINTER_TYPE },
        { .op = BF_PRESENT, .output = 1, .type = PIN_BOOL, .target = 999 },
        { .op = BF_DIV, .type = PIN_I32, .target = 999 },
        { .op = BF_WATCH_RESET },
        { .op = BF_TRACE },
        { .op = BF_ENTER },
        { .op = BF_LEAVE },
        { .op = BF_RETRIGGER, .output = 1, .type = PIN_BOOL, .left = 3 },
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        cleanup();
        builder = behavior_function_builder_new();
        slot(PIN_I32);
        slot(PIN_BOOL);
        address_slot();
        slot(PIN_F64);
        emit(invalid[i]);
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        REQUIRE(behavior_function_builder_finish(builder, error, sizeof error) == NULL);
        REQUIRE(error[0] != '\0');
    }
    cleanup();
    builder = behavior_function_builder_new();
    emit((behavior_function_instruction){ .op = BF_JUMP, .target = 0 });
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    REQUIRE(behavior_function_builder_finish(builder, error, sizeof error) == NULL);
    REQUIRE(error[0] != '\0');
    return true;
}

TEST_CASE(execution_verifier_rejects_signature_and_local_type_mismatches)
{
    const behavior_function_instruction invalid[] = {
        { .op = BF_ARGUMENT, .type = PIN_BOOL, .output = 1, .value = { .type = PIN_BOOL } },
        { .op = BF_LOCAL, .type = PIN_BOOL, .output = 1 },
        { .op = BF_SAVE, .type = PIN_BOOL, .left = 1 },
        { .op = BF_RESULT, .type = PIN_BOOL, .left = 1 },
        { .op = BF_STORE, .type = PIN_BOOL, .left = 1 },
    };
    pin_value number = { .type = PIN_I32 };
    for (int i = 0; i < 5; i++) {
        cleanup();
        builder = behavior_function_builder_new();
        behavior_function_builder_signature(builder, &number, 1, &number, 1, &number, 1);
        behavior_function_builder_local(builder, number);
        slot(PIN_I32);
        slot(PIN_BOOL);
        emit(invalid[i]);
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        REQUIRE(behavior_function_builder_finish(builder, error, sizeof error) == NULL);
        REQUIRE(error[0] != '\0');
    }
    cleanup();
    builder = behavior_function_builder_new();
    pin_value aggregate = { .type = PIN_STRUCT, .type_name = "fixture.box" };
    behavior_function_builder_signature(builder, &aggregate, 1, NULL, 0, NULL, 0);
    slot(PIN_I32);
    emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                          .type = PIN_I32,
                                          .indirect = true,
                                          .offset = sizeof(fixture_box),
                                          .value = number });
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    REQUIRE(behavior_function_builder_finish(builder, error, sizeof error) == NULL);
    REQUIRE(error[0] != '\0');
    return true;
}

TEST_CASE(execution_integer_bitwise_preserves_width_and_masked_shifts)
{
    const pin_type integers[] = { PIN_I8,  PIN_U8,  PIN_I16, PIN_U16,
                                  PIN_I32, PIN_U32, PIN_I64, PIN_U64 };
    const behavior_function_op operations[] = { BF_BIT_AND, BF_BIT_OR,
                                                BF_BIT_XOR, BF_BIT_NOT,
                                                BF_SHL,     BF_SHR };
    for (int t = 0; t < 8; t++) {
        cleanup();
        pin_type type = integers[t];
        unsigned width = (unsigned)behavior_scalar_size(type) * 8;
        uint64_t mask = UINT64_MAX >> (64 - width);
        uint64_t sign = UINT64_C(1) << (width - 1);
        pin_value arguments[2] = { { .type = type }, { .type = type } };
        pin_value results[6];
        for (int i = 0; i < 6; i++) {
            results[i] = (pin_value){ .type = type };
        }
        builder = behavior_function_builder_new();
        behavior_function_builder_signature(builder, arguments, 2, results, 6, NULL, 0);
        int left = slot(type), right = slot(type);
        emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                              .type = type,
                                              .output = left,
                                              .left = 0,
                                              .value = arguments[0] });
        emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                              .type = type,
                                              .output = right,
                                              .left = 1,
                                              .value = arguments[1] });
        for (int i = 0; i < 6; i++) {
            int output = slot(type);
            emit((behavior_function_instruction){ .op = operations[i],
                                                  .type = type,
                                                  .output = output,
                                                  .left = left,
                                                  .right = i == 3 ? -1 : right });
            emit((
                behavior_function_instruction
            ){ .op = BF_RESULT, .type = type, .left = output, .target = i });
        }
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        REQUIRE(behavior_function_compile(function));
        const struct
        {
            uint64_t left, count, shl, signed_shr, unsigned_shr;
        } cases[] = {
            { mask - 7, 1, mask - 15, mask - 3, (mask >> 1) - 3 },
            { sign, 0, sign, sign, sign },
            { sign, width, sign, sign, sign },
            { sign, width + 1, 0, sign | (sign >> 1), sign >> 1 },
            { mask, mask, sign, mask, 1 },
        };
        for (int backend = 0; backend < 2; backend++) {
            for (int c = 0; c < 5; c++) {
                arguments[0] = behavior_scalar_from_bits(type, cases[c].left);
                arguments[1] = behavior_scalar_from_bits(type, cases[c].count);
                REQUIRE(run(arguments, 2, NULL, results, backend == 1, 100) == FLOW_OK);
                const uint64_t expected[] = {
                    cases[c].left & cases[c].count,
                    (cases[c].left | cases[c].count) & mask,
                    (cases[c].left ^ cases[c].count) & mask,
                    mask ^ cases[c].left,
                    cases[c].shl,
                    behavior_scalar_signed(type) ? cases[c].signed_shr
                                                 : cases[c].unsigned_shr,
                };
                for (int i = 0; i < 6; i++) {
                    uint64_t actual = behavior_scalar_bits(results[i]) & mask;
                    if (actual != expected[i]) {
                        fprintf(
                            stderr,
                            "bitwise type=%d backend=%d case=%d op=%d "
                            "actual=%llx expected=%llx\n",
                            type,
                            backend,
                            c,
                            operations[i],
                            (unsigned long long)actual,
                            (unsigned long long)expected[i]
                        );
                    }
                    REQUIRE(actual == expected[i]);
                }
            }
        }
    }
    return true;
}

TEST_CASE(execution_bitwise_verifier_rejects_non_integer_and_mismatched_operands)
{
    const pin_type invalid[] = { PIN_F32, PIN_F64, PIN_BOOL, PIN_ENUM, PIN_HANDLE };
    for (int i = 0; i < 6; i++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_type type = i == 5 ? PIN_I32 : invalid[i];
        int left = slot(type), right = slot(i == 5 ? PIN_I64 : type),
            output = slot(type);
        emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                              .type = type,
                                              .output = left,
                                              .value = { .type = type } });
        emit((behavior_function_instruction){
            .op = BF_CONSTANT,
            .type = i == 5 ? PIN_I64 : type,
            .output = right,
            .value = { .type = i == 5 ? PIN_I64 : type } });
        emit((
            behavior_function_instruction
        ){ .op = BF_BIT_AND, .type = type, .left = left, .right = right, .output = output });
        emit((behavior_function_instruction){ .op = BF_END });
        char error[256];
        function = behavior_function_builder_finish(builder, error, sizeof error);
        REQUIRE(function == NULL && strstr(error, "bitwise"));
    }
    return true;
}

static execution_log_message logged;
static char logged_text[64], logged_name[64];
static int log_count;
static void collect_log(void *, const execution_log_message *message)
{
    log_count++;
    logged = *message;
    snprintf(logged_text, sizeof logged_text, "%s", message->text);
    snprintf(logged_name, sizeof logged_name, "%s", message->program_name);
}

TEST_CASE(execution_print_string_is_synchronous_and_preserves_context)
{
    builder = behavior_function_builder_new();
    pin_value text = { .type = PIN_STR };
    behavior_function_builder_signature(builder, &text, 1, NULL, 0, NULL, 0);
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int host = behavior_function_builder_host(builder, &EXECUTION_PrintString, &access, &text, 1, NULL, 0);
    int input = slot(PIN_STR), pointer = address_slot();
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_STR, .output = input, .value = text });
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_host_first(builder, host) });
    emit((behavior_function_instruction){ .op = BF_WRITE,
                                          .type = PIN_STR,
                                          .left = input,
                                          .right = pointer,
                                          .offset = offsetof(pin_value, str) });
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                          .node_id = 991 });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    execution_log_service sink = { .emit = collect_log };
    for (int backend = 0; backend < 2; backend++) {
        for (int mode = 0; mode < 3; mode++) {
            release_invocation();
            afree(log_services);
            if (mode != 2) {
                apush(log_services, ((service_binding){ EXECUTION_LOG_SERVICE, &sink }));
            }
            text.str = mode == 0 ? "logged before return" : NULL;
            context = (behavior_function_context){
                .arguments = &text,
                .argument_count = 1,
                .budget = 100,
                .now = 1.25,
                .program_name = "PublicLog"
            };
            behavior_function_bind_storage(&context, &storage);
            REQUIRE(behavior_function_bind(function, &context, log_services, 77));
            log_count = 0;
            REQUIRE(behavior_function_run(function, &context, backend == 1) == FLOW_OK);
            REQUIRE(log_count == (mode == 2 ? 0 : 1));
            REQUIRE(context.wake_time == 0 && context.pending_commands == 0);
            if (mode != 2) {
                REQUIRE(logged.owner == 77 && logged.node_id == 991 && logged.now == 1.25);
                REQUIRE(!strcmp(logged_name, "PublicLog"));
                REQUIRE(!strcmp(logged_text, mode == 0 ? "logged before return" : ""));
            }
        }
    }
    pin_value invalid = { .type = PIN_I32, .i = 1 };
    log_count = 0;
    REQUIRE(
        !strcmp(execution_print_string(&context, &sink, &invalid, NULL), "Print String input is not a string")
    );
    REQUIRE(log_count == 0);
    return true;
}

typedef struct
{
    const void *address;
    int visits;
} traced_address;
static void count_address(void *user, const void *address)
{
    traced_address *trace = user;
    trace->visits += address == trace->address;
}
TEST_CASE(execution_numeric_address_bits_do_not_retain_saved_storage)
{
    for (int reference = 0; reference < 2; reference++) {
        cleanup();
        builder = behavior_function_builder_new();
        pin_type type = reference ? BF_POINTER_TYPE : PIN_U64;
        pin_value initial = { .type = type };
        behavior_function_builder_signature(builder, &initial, 1, &initial, 1, NULL, 0);
        int local = behavior_function_builder_local(builder, initial);
        int input = slot(type), saved = slot(type);
        behavior_value_kind kind = reference ? BEHAVIOR_REFERENCE : BEHAVIOR_VALUE;
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_ARGUMENT, 0, kind));
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_RESULT, 0, kind));
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_LOCAL, local, kind));
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, input, kind));
        REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, saved, kind));
        emit((
            behavior_function_instruction
        ){ .op = BF_ARGUMENT, .type = type, .output = input, .value = initial });
        emit((
            behavior_function_instruction
        ){ .op = BF_SAVE, .type = type, .target = local, .left = input });
        emit((
            behavior_function_instruction
        ){ .op = BF_LOCAL, .type = type, .left = local, .output = saved });
        emit((behavior_function_instruction){ .op = BF_RESULT, .type = type, .left = saved });
        emit((behavior_function_instruction){ .op = BF_RETURN });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        REQUIRE(behavior_function_kind(function, BEHAVIOR_BANK_LOCAL, local) == kind);
        REQUIRE(behavior_function_compile(function));
        void *allocation = arena_alloc(&managed, 32);
        pin_value argument = { .type = type, .u64 = (uint64_t)(uintptr_t)allocation };
        for (int backend = 0; backend < 2; backend++) {
            pin_value result = initial;
            REQUIRE(run(&argument, 1, NULL, &result, backend == 1, 100) == FLOW_OK);
            REQUIRE(context.returned && result.u64 == argument.u64);
            traced_address trace = { .address = allocation };
            behavior_function_trace_values(function, &context, count_address, &trace);
            REQUIRE(trace.visits == (reference ? 2 : 0));
            REQUIRE(
                behavior_function_retains_storage(function, &context, &managed) ==
                (reference != 0)
            );
        }
    }
    return true;
}

TEST_CASE(execution_unary_numeric_operands_ignore_unrelated_address_slot)
{
    builder = behavior_function_builder_new();
    pin_value initial[] = { { .type = PIN_F64 },
                            { .type = PIN_I32 },
                            { .type = PIN_F32 },
                            { .type = PIN_F64 } };
    behavior_function_builder_signature(builder, NULL, 0, initial, 4, NULL, 0);
    REQUIRE(address_slot() == 0);
    int value = slot(PIN_F64), negative = slot(PIN_F64);
    int integer = slot(PIN_I32), narrowed = slot(PIN_F32);
    int absolute = slot(PIN_F64);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_F64,
                                          .output = value,
                                          .value = { .type = PIN_F64, .d = 5.5 } });
    emit((
        behavior_function_instruction
    ){ .op = BF_NEG, .type = PIN_F64, .left = value, .output = negative });
    emit((
        behavior_function_instruction
    ){ .op = BF_CAST, .type = PIN_I32, .left = negative, .output = integer });
    emit((
        behavior_function_instruction
    ){ .op = BF_CONVERT, .type = PIN_F32, .left = negative, .output = narrowed });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_F64, .left = negative, .target = 0 });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_I32, .left = integer, .target = 1 });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_F32, .left = narrowed, .target = 2 });
    emit((
        behavior_function_instruction
    ){ .op = BF_ABS, .type = PIN_F64, .left = negative, .output = absolute });
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = PIN_F64, .left = absolute, .target = 3 });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        pin_value results[4];
        memcpy(results, initial, sizeof results);
        REQUIRE(run(NULL, 0, NULL, results, backend == 1, 100) == FLOW_OK);
        REQUIRE(context.returned);
        REQUIRE(results[0].type == PIN_F64 && results[0].d == -5.5);
        REQUIRE(results[1].type == PIN_I32 && results[1].i == -5);
        REQUIRE(results[2].type == PIN_F32 && results[2].f == -5.5f);
        REQUIRE(results[3].type == PIN_F64 && results[3].d == 5.5);
    }
    return true;
}

static int selected_action_outcome;
static const char *select_action(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *arguments,
    int argument_count,
    pin_value *results,
    int result_count,
    const uint64_t *outcomes,
    int outcome_count,
    int *selected
)
{
    (void)service;
    (void)arguments;
    if (!ctx || ctx->node_id != 991 || argument_count != 0 ||
        result_count != 1 || results[0].type != PIN_I32 || outcome_count != 2 ||
        outcomes[0] != 101 || outcomes[1] != 202) {
        return "Action signature or owned outcome IDs changed";
    }
    results[0].i = 37;
    *selected = selected_action_outcome;
    return NULL;
}
TEST_CASE(execution_synchronous_action_owns_outcomes_and_validates_selection)
{
    builder = behavior_function_builder_new();
    pin_value result = { .type = PIN_I32 };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    const node_host_call call = { .action = select_action };
    const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    int host = behavior_function_builder_host(builder, &call, &access, NULL, 0, &result, 1);
    uint64_t duplicate[] = { 101, 101 }, zero[] = { 0, 202 };
    REQUIRE(!behavior_function_builder_host_outcomes(builder, host, duplicate, 2));
    REQUIRE(!behavior_function_builder_host_outcomes(builder, host, zero, 2));
    uint64_t outcomes[] = { 101, 202 };
    REQUIRE(behavior_function_builder_host_outcomes(builder, host, outcomes, 2));
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_FLOW,
                                          .node_id = 991 });
    int pointer = address_slot(), value = slot(PIN_I32);
    emit((behavior_function_instruction){
        .op = BF_HOST_SLOT,
        .type = BF_POINTER_TYPE,
        .output = pointer,
        .left = behavior_function_builder_host_results(builder, host) });
    emit((behavior_function_instruction){ .op = BF_READ,
                                          .type = PIN_I32,
                                          .output = value,
                                          .left = pointer,
                                          .offset = offsetof(pin_value, i) });
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = value });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    outcomes[0] = outcomes[1] = 999;
    behavior_function_builder_free(builder);
    builder = NULL;
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        pin_value output = result;
        selected_action_outcome = 1;
        REQUIRE(run(NULL, 0, NULL, &output, backend == 1, 100) == FLOW_OK);
        REQUIRE(context.returned_outcome == 202);
        REQUIRE(output.type == PIN_I32 && output.i == 37);
        output = result;
        selected_action_outcome = 2;
        REQUIRE(run(NULL, 0, NULL, &output, backend == 1, 100) == FLOW_ERR_NODE);
        REQUIRE(
            context.error_message &&
            !strcmp(context.error_message, "Action selected an invalid outcome")
        );
        REQUIRE(context.returned_outcome == 0);
        REQUIRE(output.type == PIN_I32 && output.i == 0);
    }
    return true;
}

/* The result bit runs an intrinsic in the caller's storage, and only those
 * that may build there; union_keep runs only there. It keeps the payload
 * values its mask names when they are held in storage and copies the rest. */
TEST_CASE(execution_intrinsics_build_in_result_storage)
{
    uint64_t push = execution_intrinsic_id(&EXECUTION_array_push_call),
             make = execution_intrinsic_id(&EXECUTION_union_make_call),
             keep = execution_intrinsic_id(&EXECUTION_union_keep_call);
    REQUIRE(
        execution_intrinsic_resolve(push | EXECUTION_INTRINSIC_IN_RESULT) == &EXECUTION_array_push_call
    );
    REQUIRE(
        execution_intrinsic_access(push | EXECUTION_INTRINSIC_IN_RESULT) == &EXECUTION_assignment_access
    );
    REQUIRE(!execution_intrinsic_resolve(make | EXECUTION_INTRINSIC_IN_RESULT));
    REQUIRE(!execution_intrinsic_resolve(keep));
    REQUIRE(
        execution_intrinsic_resolve(keep | EXECUTION_INTRINSIC_IN_RESULT) == &EXECUTION_union_keep_call
    );
    REQUIRE(execution_intrinsic_abi(keep | EXECUTION_INTRINSIC_IN_RESULT) == 1);
    arena storage = { 0 }, other = { 0 };
    pin_array *held = pin_array_snapshot_allocate(&storage, PIN_I32, NULL, 1),
              *foreign = pin_array_snapshot_allocate(&other, PIN_I32, NULL, 1);
    held->items[0] = (pin_value){ .type = PIN_I32, .i = 1 };
    foreign->items[0] = (pin_value){ .type = PIN_I32, .i = 2 };
    pin_value args[] = { { .type = PIN_U64, .u64 = 7 },
                         { .type = PIN_U64, .u64 = 3 },
                         { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = held },
                         { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = foreign },
                         { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = held } },
              out = { .type = PIN_UNION };
    REQUIRE(!EXECUTION_union_keep_call.value(&storage, args, 5, &out));
    const pin_union *value = out.ptr;
    REQUIRE(value->tag == 7 && value->count == 3);
    /* The first is kept; the second is not the storage's, and the mask leaves
     * out the third. */
    REQUIRE(value->values[0].ptr == held);
    REQUIRE(
        value->values[1].ptr != foreign &&
        arena_contains(&storage, value->values[1].ptr)
    );
    REQUIRE(
        value->values[2].ptr != held &&
        arena_contains(&storage, value->values[2].ptr)
    );
    REQUIRE(pin_array_get(value->values[1].ptr, 0).i == 2);
    arena_free(&storage);
    arena_free(&other);
    return true;
}
TEST_CASE(execution_union_extraction_checks_dynamic_payload_type)
{
    pin_value payload = { .type = PIN_STR, .str = "owned payload" }, value;
    REQUIRE(!pin_union_value(&managed, 42, &payload, 1, &value));
    builder = behavior_function_builder_new();
    pin_value args[] = { value,
                         { .type = PIN_U64, .u64 = 42 },
                         { .type = PIN_I32 },
                         { .type = PIN_BOOL } };
    pin_value output = { .type = PIN_F64 };
    args[3].b = true;
    REQUIRE(EXECUTION_union_get_call.value(&managed, args, 4, &output));
    REQUIRE(output.type == PIN_F64);
    args[3].b = false;
    pin_value invalid_tag = { .type = PIN_U64 };
    REQUIRE(EXECUTION_union_make_call.value(&managed, &invalid_tag, 1, &output));
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_union_get_call,
        &EXECUTION_value_access,
        args,
        4,
        &output,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_union_get_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .target = host,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(run(NULL, 0, NULL, NULL, backend == 1, 100) == FLOW_ERR_INVALID_GRAPH);
        REQUIRE(
            context.error_message &&
            !strcmp(context.error_message, "Union payload differs from the extraction type")
        );
    }
    return true;
}

TEST_CASE(execution_prepared_identity_includes_instruction_schema)
{
    enum
    {
        SCHEMA_COUNT = BF_END + 1,
        MAX_BYTES = 4096
    };
    const char *names[SCHEMA_COUNT] = {
#define FUNCTION_NAME(op) [BF_##op] = #op,
#define MATH_NAME(op, name, fn) FUNCTION_NAME(op)
        BEHAVIOR_FUNCTION_OPS(FUNCTION_NAME, MATH_NAME)
#undef MATH_NAME
#undef FUNCTION_NAME
    };
    const char *foreign[SCHEMA_COUNT];
    memcpy(foreign, names, sizeof foreign);
    foreign[BF_REQUIRE] = names[BF_ASSERT];
    foreign[BF_ASSERT] = names[BF_REQUIRE];
    REQUIRE(sizeof names == sizeof foreign);
    REQUIRE(!strcmp(names[BF_END], foreign[BF_END]));
    REQUIRE(!strcmp(names[BF_REQUIRE], "REQUIRE"));
    REQUIRE(!strcmp(foreign[BF_REQUIRE], "ASSERT"));
    uint64_t current_abi = prepared_abi(names, SCHEMA_COUNT);
    uint64_t foreign_abi = prepared_abi(foreign, SCHEMA_COUNT);
    REQUIRE(current_abi != foreign_abi);
    REQUIRE(current_abi == prepared_abi(names, SCHEMA_COUNT));

    builder = behavior_function_builder_new();
    int value = slot(PIN_BOOL);
    emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                          .type = PIN_BOOL,
                                          .output = value,
                                          .value = { .type = PIN_BOOL, .b = false } });
    /* Under the swapped mapping this valid REQUIRE would become ASSERT with
     * target BUDGET, changing DATA failure into a step-budget failure. */
    emit((behavior_function_instruction){ .op = BF_REQUIRE,
                                          .type = PIN_BOOL,
                                          .left = value,
                                          .target = BEHAVIOR_FUNCTION_BUDGET,
                                          .pin = 7 });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(acount(function->instructions) == 3);
    prepared_plan = execution_module_plan_new();
    prepared_plan->id = 1;
    prepared_plan->valid = true;
    apush(
        prepared_plan->functions,
        ((execution_module_function){ .id = 1,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = function })
    );
    function = NULL; /* The plan now owns the definition. */
    prepared_plan->function_count = 1;
    char error[192] = { 0 };
    prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(prepared_module && !prepared_plan);
    size_t length = 0;
    REQUIRE(
        execution_prepared_write(prepared_module, &prepared_bytes, &length, error, sizeof error)
    );
    REQUIRE(length > 0 && length < MAX_BYTES);
    execution_module_free(prepared_module);
    prepared_module = NULL;
    behavior_function_builder_free(builder);
    builder = NULL;

    /* Decode after retiring the producer, and retain its original failure kind. */
    prepared_module = execution_prepared_read(prepared_bytes, length, NULL, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(prepared_module);
    REQUIRE(execution_module_function_count(prepared_module) == 1);
    REQUIRE(execution_module_import_count(prepared_module) == 0);
    const behavior_function *decoded = execution_module_function_at(prepared_module, 0)
                                           .body;
    REQUIRE(acount(decoded->instructions) == 3);
    REQUIRE(decoded->instructions[1].op == BF_REQUIRE);
    context = (behavior_function_context){ .budget = 16 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(decoded, &context, NULL, 0));
    REQUIRE(behavior_function_run(decoded, &context, false) == FLOW_ERR_INVALID_GRAPH);
    REQUIRE(context.failure == BEHAVIOR_FUNCTION_DATA && context.error_pin == 7);
    release_invocation();
    execution_module_free(prepared_module);
    prepared_module = NULL;

    execution_wire wire = { .data = prepared_bytes, .size = length, .reading = true };
    uint64_t header[4] = { 0 };
    for (int i = 0; i < 4; i++) {
        execution_wire_u64(&wire, &header[i]);
    }
    REQUIRE(!wire.error && wire.offset < length);
    REQUIRE(header[1] == EXECUTION_PREPARED_VERSION && header[2] == sizeof(void *));
    /* This fails with the original layout-only header, not an arbitrary bit flip. */
    REQUIRE(header[3] == current_abi);
    size_t payload_offset = wire.offset;
    wire = (execution_wire){ 0 };
    for (int i = 0; i < 4; i++) {
        uint64_t field = i == 3 ? foreign_abi : header[i];
        execution_wire_u64(&wire, &field);
    }
    prepared_mutation = wire.data;
    REQUIRE(!wire.error);
    for (size_t i = payload_offset; i < length; i++) {
        apush(prepared_mutation, prepared_bytes[i]);
    }
    REQUIRE(acount(prepared_mutation) < MAX_BYTES);
    const size_t budget = 1024 * 1024;
    size_t remaining = budget;
    xalloc_stats before, after;
    xalloc_stats_get(&before);
    prepared_module = execution_prepared_read_budget(
        prepared_mutation,
        (size_t)acount(prepared_mutation),
        NULL,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        sizeof error
    );
    xalloc_stats_get(&after);
    REQUIRE(!prepared_module);
    REQUIRE(!strcmp(error, "prepared target ABI differs"));
    REQUIRE(remaining == budget);
    /* Only the empty plan was allocated: no function/artifact decoding or
     * definition finalization can precede the schema mismatch. */
    REQUIRE(after.calls - before.calls == 1);
    REQUIRE(after.requested_bytes - before.requested_bytes == sizeof(execution_module_plan));
    return true;
}

TEST_CASE(execution_decoded_suspensions_charge_exact_live_sets)
{
    /* A wide chain followed by many pauses keeps one value live. Reserving a
     * full live-value vector per pause needed about 4.2 MiB here. */
    enum
    {
        WIDE_SLOTS = 1024,
        WIDE_PAUSES = 512
    };
    const size_t budget = 2 * 1024 * 1024;
    pin_value number = { .type = PIN_I32 };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &number, 1, &number, 1, NULL, 0);
    int input = slot(PIN_I32), sum = input;
    emit((
        behavior_function_instruction
    ){ .op = BF_ARGUMENT, .type = PIN_I32, .output = input, .value = number });
    for (int i = 1; i < WIDE_SLOTS; i++) {
        int next = slot(PIN_I32);
        emit((
            behavior_function_instruction
        ){ .op = BF_ADD, .type = PIN_I32, .output = next, .left = sum, .right = input });
        sum = next;
    }
    for (int i = 0; i < WIDE_PAUSES; i++) {
        emit((behavior_function_instruction){ .op = BF_SUSPEND });
    }
    emit((behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = sum });
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    REQUIRE(finish());
    REQUIRE(acount(function->slots) == WIDE_SLOTS && acount(function->suspensions) == WIDE_PAUSES);
    prepared_plan = execution_module_plan_new();
    prepared_plan->id = 1;
    prepared_plan->valid = true;
    apush(
        prepared_plan->functions,
        ((execution_module_function){ .id = 1,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = function })
    );
    function = NULL;
    prepared_plan->function_count = 1;
    char error[192] = { 0 };
    prepared_module = execution_module_finish(&prepared_plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(prepared_module);
    size_t length = 0;
    REQUIRE(
        execution_prepared_write(prepared_module, &prepared_bytes, &length, error, sizeof error)
    );
    execution_module_free(prepared_module);
    prepared_module = NULL;
    behavior_function_builder_free(builder);
    builder = NULL;
    for (int backend = 0; backend < 2; backend++) {
        size_t remaining = budget;
        prepared_module = execution_prepared_read_budget(
            prepared_bytes,
            length,
            NULL,
            backend ? TEST_COMPILED_BACKEND : BEHAVIOR_BYTECODE,
            &remaining,
            error,
            sizeof error
        );
        if (!prepared_module) {
            fprintf(stderr, "decoded wide suspensions: %s\n", error);
        }
        REQUIRE(prepared_module);
        REQUIRE(budget - remaining == execution_module_storage_bytes(prepared_module));
        const behavior_function
            *decoded = execution_module_function_at(prepared_module, 0).body;
        REQUIRE(acount(decoded->suspensions) == WIDE_PAUSES);
        for (int i = 0; i < WIDE_PAUSES; i++) {
            const behavior_function_suspension *s = &decoded->suspensions[i];
            REQUIRE(s->value_count == 1 && s->values[0] == sum && !s->root_count);
            REQUIRE(arena_contains(&decoded->owned, s->values));
        }
        /* run() borrows the module's definition; cleanup must never free it. */
        function = (behavior_function *)decoded;
        pin_value argument = { .type = PIN_I32, .i = 3 }, result = number;
        int pauses = 0,
            flow = run(&argument, 1, NULL, &result, backend == 1, 1u << 20);
        while (flow == FLOW_SUSPENDED_RESULT) {
            pauses++;
            flow = behavior_function_run(function, &context, backend == 1);
        }
        release_invocation();
        function = NULL;
        REQUIRE(flow == FLOW_OK && pauses == WIDE_PAUSES && result.i == 3 * WIDE_SLOTS);
        execution_module_free(prepared_module);
        prepared_module = NULL;
    }
    return true;
}

typedef struct
{
    pin_value input;
    pin_type to;
    behavior_cast_mode mode;
    bool fits;
    uint64_t bits; /* Integer result bits, or the double's representation. */
} cast_vector;
static pin_value cast_input(pin_type type, uint64_t bits)
{
    return behavior_scalar_from_bits(type, bits);
}
static pin_value cast_real(pin_type type, double x)
{
    return type == PIN_F32 ? (pin_value){ .type = PIN_F32, .f = (float)x }
                           : (pin_value){ .type = PIN_F64, .d = x };
}
static uint64_t cast_double_bits(double x)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof bits);
    return bits;
}
static uint64_t cast_result_bits(pin_value value)
{
    return value.type == PIN_F64 ? cast_double_bits(value.d)
                                 : behavior_scalar_bits(value);
}
TEST_CASE(execution_numeric_casts_apply_exact_wrap_and_clamp)
{
    enum
    {
        E = BEHAVIOR_CAST_EXACT,
        W = BEHAVIOR_CAST_WRAP,
        C = BEHAVIOR_CAST_CLAMP
    };
    const uint64_t big = UINT64_C(1) << 40, top = UINT64_C(1) << 63;
    const cast_vector vectors[] = {
        { cast_input(PIN_I64, 300), PIN_I32, E, true, 300 },
        { cast_input(PIN_I64, big), PIN_I32, E, false, 0 },
        { cast_input(PIN_I64, big + 7), PIN_I32, W, true, 7 },
        { cast_input(PIN_I64, big), PIN_I32, C, true, INT32_MAX },
        { cast_input(PIN_I64, (uint64_t)-(int64_t)big), PIN_I32, C, true, (uint64_t)(int64_t)INT32_MIN },
        { cast_input(PIN_U64, 255), PIN_U8, E, true, 255 },
        { cast_input(PIN_U64, 300), PIN_U8, E, false, 0 },
        { cast_input(PIN_U64, 300), PIN_U8, W, true, 44 },
        { cast_input(PIN_U64, 300), PIN_U8, C, true, 255 },
        { cast_input(PIN_I32, (uint64_t)-1), PIN_U64, E, false, 0 },
        { cast_input(PIN_I32, (uint64_t)-1), PIN_U64, W, true, UINT64_MAX },
        { cast_input(PIN_I32, (uint64_t)-1), PIN_U64, C, true, 0 },
        { cast_input(PIN_I32, 5), PIN_U64, E, true, 5 },
        { cast_input(PIN_U64, top), PIN_I64, E, false, 0 },
        { cast_input(PIN_U64, top), PIN_I64, C, true, INT64_MAX },
        { cast_input(PIN_U64, top), PIN_I64, W, true, top },
        { cast_input(PIN_I8, (uint64_t)-1), PIN_U16, E, false, 0 },
        { cast_input(PIN_I8, (uint64_t)-1), PIN_I32, E, true, UINT64_MAX },
        { cast_input(PIN_U8, 200), PIN_I32, E, true, 200 },
        { cast_input(PIN_I16, 200), PIN_I8, W, true, (uint64_t)-56 },
        { cast_input(PIN_U32, 0x80000000u), PIN_I32, E, false, 0 },
        { cast_input(PIN_U32, 0x80000000u), PIN_I32, C, true, INT32_MAX },
        { cast_real(PIN_F64, 0x1p63 + 4096), PIN_U64, E, true, top + 4096 },
        { cast_real(PIN_F64, 1e30), PIN_U64, E, true, UINT64_MAX },
        { cast_real(PIN_F64, -5), PIN_U64, E, true, 0 },
        { cast_real(PIN_F64, NAN), PIN_I32, E, true, 0 },
        { cast_real(PIN_F64, -40000.5), PIN_I16, E, true, (uint64_t)(int64_t)INT16_MIN },
        { cast_real(PIN_F64, 123.9), PIN_I16, E, true, 123 },
        { cast_real(PIN_F32, 2.75), PIN_I32, E, true, 2 },
        { cast_real(PIN_F64, 0x1p63), PIN_I64, E, true, INT64_MAX },
        { cast_real(PIN_F64, -1e30), PIN_I64, E, true, (uint64_t)INT64_MIN },
        { cast_real(PIN_F32, 0x1p63), PIN_U64, E, true, top },
        { cast_real(PIN_F64, -0.0), PIN_U8, E, true, 0 },
        { cast_input(PIN_U64, UINT64_MAX), PIN_F64, E, true, cast_double_bits(0x1p64) },
        { cast_input(PIN_I64, (uint64_t)-3), PIN_F64, E, true, cast_double_bits(-3) },
    };
    for (size_t v = 0; v < sizeof vectors / sizeof *vectors; v++) {
        const cast_vector *c = &vectors[v];
        /* A failing cast keeps its check even when nothing reads its value. */
        for (int unused = 0; unused <= !c->fits; unused++) {
            pin_value from = { .type = c->input.type }, to = { .type = c->to };
            pin_value returned = unused ? from : to;
            builder = behavior_function_builder_new();
            behavior_function_builder_signature(builder, &from, 1, &returned, 1, NULL, 0);
            int input = slot(from.type), output = slot(c->to);
            emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                                  .type = from.type,
                                                  .output = input,
                                                  .value = from });
            emit((behavior_function_instruction){ .op = BF_CAST,
                                                  .type = c->to,
                                                  .output = output,
                                                  .left = input,
                                                  .target = c->mode,
                                                  .pin = 3 });
            emit((behavior_function_instruction){ .op = BF_RESULT,
                                                  .type = returned.type,
                                                  .left = unused ? input : output });
            emit((behavior_function_instruction){ .op = BF_RETURN });
            emit((behavior_function_instruction){ .op = BF_END });
            REQUIRE(finish());
            REQUIRE(behavior_function_compile(function));
            for (int backend = 0; backend < 2; backend++) {
                pin_value result = returned;
                int flow = run(&c->input, 1, NULL, &result, backend == 1, 16);
                if (!c->fits) {
                    REQUIRE(flow == FLOW_ERR_INVALID_GRAPH);
                    REQUIRE(context.failure == BEHAVIOR_FUNCTION_DATA && context.error_pin == 3);
                    continue;
                }
                if (flow != FLOW_OK || cast_result_bits(result) != c->bits) {
                    fprintf(
                        stderr,
                        "cast vector %zu backend %d: flow %d bits %llx\n",
                        v,
                        backend,
                        flow,
                        (unsigned long long)cast_result_bits(result)
                    );
                }
                REQUIRE(
                    flow == FLOW_OK && result.type == c->to &&
                    cast_result_bits(result) == c->bits
                );
            }
            release_invocation();
            behavior_function_free(function);
            function = NULL;
            behavior_function_builder_free(builder);
            builder = NULL;
        }
    }
    return true;
}

void test_language_execution()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(execution_prepared_identity_includes_instruction_schema);
    RUN_TEST_CASE(execution_union_extraction_checks_dynamic_payload_type);
    RUN_TEST_CASE(execution_intrinsics_build_in_result_storage);
    RUN_TEST_CASE(execution_branches_state_and_host_calls);
    RUN_TEST_CASE(execution_source_inspection_is_lazy_and_preserves_resume);
    RUN_TEST_CASE(execution_costs_verify_intrinsics_and_follow_recursive_calls);
    RUN_TEST_CASE(execution_costs_include_managed_scalar_result_and_state_escape);
    RUN_TEST_CASE(execution_finished_definition_owns_producer_strings);
    RUN_TEST_CASE(execution_verifier_rejects_invalid_lowering);
    RUN_TEST_CASE(execution_budget_limits_checked_back_edges);
    RUN_TEST_CASE(execution_verifier_rejects_invalid_banks_and_metadata);
    RUN_TEST_CASE(execution_verifier_rejects_invalid_saved_aggregate_roots);
    RUN_TEST_CASE(execution_saved_aggregate_roots_keep_only_live_shapes);
    RUN_TEST_CASE(execution_verifier_rejects_signature_and_local_type_mismatches);
    RUN_TEST_CASE(execution_linked_function_returns_checked_value);
    RUN_TEST_CASE(execution_reused_helper_resets_host_bank_and_rebinds_service);
    RUN_TEST_CASE(execution_reused_host_storage_clears_context_and_rebinds_fixed_buffers);
    RUN_TEST_CASE(execution_integer_bitwise_preserves_width_and_masked_shifts);
    RUN_TEST_CASE(execution_bitwise_verifier_rejects_non_integer_and_mismatched_operands);
    RUN_TEST_CASE(execution_print_string_is_synchronous_and_preserves_context);
    RUN_TEST_CASE(execution_numeric_address_bits_do_not_retain_saved_storage);
    RUN_TEST_CASE(execution_unary_numeric_operands_ignore_unrelated_address_slot);
    RUN_TEST_CASE(execution_synchronous_action_owns_outcomes_and_validates_selection);
    RUN_TEST_CASE(execution_decoded_suspensions_charge_exact_live_sets);
    RUN_TEST_CASE(execution_numeric_casts_apply_exact_wrap_and_clamp);
    pu_clear_setup();
}
