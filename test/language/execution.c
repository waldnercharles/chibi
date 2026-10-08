#include "execution/function_builder.h"
#include "execution/log.h"
#include "execution/scalar.h"
#include "execution/service.h"
#include "execution/status.h"
#include "graph/shader/shader_graph.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct
{
    int32_t value;
} execution_box;
static const type_field box_field = { .name = "value",
                                      .type = PIN_I32,
                                      .size = sizeof(int32_t),
                                      .type_name = "int" };
static const type_desc box_type = { .name = "fixture.box",
                                    .size = sizeof(execution_box),
                                    .fields = &box_field,
                                    .field_count = 1 };
static const type_desc *const types[] = { &box_type };
type_catalog_pack test_program_types();
type_catalog_pack test_graph_model_types();

int type_catalog_pack_count()
{
    return 4;
}

type_catalog_pack type_catalog_pack_at(int index)
{
    assert(index >= 0 && index < 4);
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
    REQUIRE(behavior_function_entry(function) != NULL);
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
    REQUIRE(behavior_function_compile(callee));
    REQUIRE(behavior_function_compile(function));
    for (int backend = 0; backend < 2; backend++) {
        pin_value result = number;
        REQUIRE(run(NULL, 0, NULL, &result, backend == 1, 100) == FLOW_OK);
        REQUIRE(result.type == PIN_I32 && result.i == 42);
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
                                          .offset = sizeof(execution_box),
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

void test_language_execution()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(execution_branches_state_and_host_calls);
    RUN_TEST_CASE(execution_finished_definition_owns_producer_strings);
    RUN_TEST_CASE(execution_verifier_rejects_invalid_lowering);
    RUN_TEST_CASE(execution_budget_limits_checked_back_edges);
    RUN_TEST_CASE(execution_verifier_rejects_invalid_banks_and_metadata);
    RUN_TEST_CASE(execution_verifier_rejects_signature_and_local_type_mismatches);
    RUN_TEST_CASE(execution_linked_function_returns_checked_value);
    RUN_TEST_CASE(execution_integer_bitwise_preserves_width_and_masked_shifts);
    RUN_TEST_CASE(execution_bitwise_verifier_rejects_non_integer_and_mismatched_operands);
    RUN_TEST_CASE(execution_print_string_is_synchronous_and_preserves_context);
    RUN_TEST_CASE(execution_numeric_address_bits_do_not_retain_saved_storage);
    RUN_TEST_CASE(execution_unary_numeric_operands_ignore_unrelated_address_slot);
    RUN_TEST_CASE(execution_synchronous_action_owns_outcomes_and_validates_selection);
    pu_clear_setup();
}
