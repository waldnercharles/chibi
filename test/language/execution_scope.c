#include "execution/backend.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/module_builder.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "runtime/native_capabilities.h"
#include "runtime/native_definitions.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* No source compiler, resources, workers or I/O. Every invocation is capped at
 * 128 instructions; the complete selector makes fewer than 64 invocations. */
static behavior_function *scalar, *caller;
static compiler_host *value_host;
static native_capabilities *value_native;
static execution_module *value_module;
static unsigned char *value_blob, *value_artifact;
static size_t value_artifact_size;
static compiler_host_result value_result;
static int task_starts;
static void cleanup()
{
    free(value_result.json);
    value_result = (compiler_host_result){ 0 };
    compiler_host_free(value_host);
    value_host = NULL;
    execution_module_free(value_module);
    value_module = NULL;
    native_capabilities_free(value_native);
    value_native = NULL;
    free(value_blob);
    free(value_artifact);
    value_blob = value_artifact = NULL;
    behavior_function_free(caller);
    behavior_function_free(scalar);
    caller = scalar = NULL;
    task_starts = 0;
}
static void emit(behavior_function_builder *b, behavior_function_instruction instruction)
{
    instruction.source = 1;
    instruction.node_id = 1000 + instruction.op;
    behavior_function_builder_emit(b, instruction);
}
static behavior_function_builder *builder(bool arguments)
{
    behavior_function_builder *b = behavior_function_builder_new();
    pin_value number = { .type = PIN_F64 };
    behavior_function_builder_signature(
        b,
        arguments ? &number : NULL,
        arguments ? 1 : 0,
        &number,
        1,
        NULL,
        0
    );
    int source = behavior_function_builder_origin(
        b,
        (
            behavior_function_source
        ){ .parent = -1, .asset_name = "execution-scope.chibi", .line = 7, .column = 3 }
    );
    assert(source == 1);
    return b;
}
static behavior_function *finish(behavior_function_builder *b)
{
    char error[256];
    behavior_function *f = behavior_function_builder_finish(b, error, sizeof error);
    if (!f) {
        fprintf(stderr, "execution scope fixture: %s\n", error);
    }
    behavior_function_builder_free(b);
    return f;
}
static bool prepare()
{
    behavior_function_builder *b = builder(true);
    int input = behavior_function_builder_slot(b, PIN_F64);
    int two = behavior_function_builder_slot(b, PIN_F64);
    int product = behavior_function_builder_slot(b, PIN_F64);
    int dead = behavior_function_builder_slot(b, PIN_F64);
    emit(
        b,
        (behavior_function_instruction){ .op = BF_ARGUMENT,
                                         .type = PIN_F64,
                                         .output = input,
                                         .value = { .type = PIN_F64 } }
    );
    emit(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .output = two,
                                         .value = { .type = PIN_F64, .d = 2 } }
    );
    /* The compact runtime drops this operation. Scoped execution must charge
     * the canonical operation, including with the bytecode backend. */
    emit(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .output = dead,
                                         .value = { .type = PIN_F64, .d = 99 } }
    );
    emit(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_MUL, .type = PIN_F64, .output = product, .left = input, .right = two }
    );
    emit(b, (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = product });
    emit(b, (behavior_function_instruction){ .op = BF_RETURN });
    emit(b, (behavior_function_instruction){ .op = BF_END });
    scalar = finish(b);
    REQUIRE(scalar);
    REQUIRE(acount(scalar->instructions) <= 24);
    REQUIRE(acount(scalar->runtime_instructions) < acount(scalar->instructions));

    b = builder(false);
    pin_value number = { .type = PIN_F64 }, argument = { .type = PIN_F64, .d = 21 };
    int call = behavior_function_builder_call(b, scalar, &argument, 1, &number, 1);
    int address = behavior_function_builder_slot(b, BF_POINTER_TYPE);
    REQUIRE(behavior_function_builder_kind(b, BEHAVIOR_BANK_SLOT, address, BEHAVIOR_ADDRESS));
    int result = behavior_function_builder_slot(b, PIN_F64);
    emit(b, (behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit(b, (behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit(
        b,
        (behavior_function_instruction){
            .op = BF_HOST_SLOT,
            .type = BF_POINTER_TYPE,
            .output = address,
            .left = behavior_function_builder_call_results(b, call) }
    );
    emit(
        b,
        (behavior_function_instruction){ .op = BF_READ,
                                         .type = PIN_F64,
                                         .output = result,
                                         .left = address,
                                         .offset = offsetof(pin_value, d) }
    );
    emit(b, (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = result });
    emit(b, (behavior_function_instruction){ .op = BF_RETURN });
    emit(b, (behavior_function_instruction){ .op = BF_END });
    caller = finish(b);
    REQUIRE(caller);
    REQUIRE(acount(caller->instructions) <= 24);
    return true;
}
typedef struct
{
    int status, source, frame, stack, error_pin, pc;
    uint64_t node_id;
    uintptr_t steps, error_steps;
    const behavior_function *source_function;
    double value;
    bool returned;
    /* The message lives in the run's storage, so it is copied out. */
    bool has_message;
    char message[256];
    behavior_run_counts counts;
} observation;
static int start_task(behavior_function_context *context, int call)
{
    assert(context->activation->pc && call == 0);
    task_starts++;
    return FLOW_SUSPENDED_RESULT;
}
static observation invoke(const behavior_function *f)
{
    arena storage = { 0 };
    arena_init(&storage, 512);
    behavior_function_activation activation = { 0 };
    bool resumable = behavior_function_resumable(f);
    if (resumable) {
        behavior_function_activation_init(f, &activation);
    }
    pin_value argument = { .type = PIN_F64, .d = 21 }, result = { .type = PIN_F64 };
    behavior_function_prepare_results(f, &result, &storage);
    behavior_function_context context = { .arguments = &argument,
                                          .argument_count = f == scalar ? 1 : 0,
                                          .results = &result,
                                          .budget = 128,
                                          .steps = 5,
                                          .start_task = start_task,
                                          .activation = resumable ? &activation : NULL };
    behavior_function_bind_storage(&context, &storage);
    assert(behavior_function_bind(f, &context, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(f, &context, false);
    behavior_run_scope_end(&runs);
    observation out = { .status = status,
                        .source = context.error_source,
                        .error_pin = context.error_pin,
                        .node_id = context.node_id,
                        .steps = context.steps,
                        .error_steps = context.error_steps,
                        .pc = activation.pc,
                        .source_function = context.source_function,
                        .frame = context.error_frame,
                        .stack = context.error_stack,
                        .value = result.d,
                        .returned = context.returned,
                        .has_message = context.error_message != NULL,
                        .counts = runs.counts };
    if (out.has_message) {
        snprintf(out.message, sizeof out.message, "%s", context.error_message);
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    return out;
}
static observation run(const behavior_function *f, behavior_execution_scope *scope)
{
    behavior_execution_scope_begin(scope);
    observation out = invoke(f);
    behavior_execution_scope_end(scope);
    return out;
}
static bool cancelled(void *user)
{
    const behavior_execution_scope *scope = user;
    return scope->remaining <= 125;
}
static bool parity(const behavior_function *root, const observation *a, const observation *b)
{
    REQUIRE(a->status == b->status);
    REQUIRE(a->returned == b->returned);
    REQUIRE(a->source == b->source && a->frame == b->frame && a->stack == b->stack);
    REQUIRE(a->node_id == b->node_id && a->error_pin == b->error_pin);
    REQUIRE(a->steps == b->steps && a->error_steps == b->error_steps);
    REQUIRE(a->source_function == b->source_function);
    REQUIRE(a->has_message == b->has_message);
    REQUIRE(!strcmp(a->message, b->message));
    if (a->status == FLOW_OK) {
        REQUIRE(a->returned && a->value == 42 && b->value == 42);
    } else {
        REQUIRE(a->node_id && a->error_pin == -1);
        REQUIRE(a->steps == 5);
        /* Outlined callees have their own step domain; the root keeps its
         * prior work while error_steps describes the precise failing frame. */
        REQUIRE(a->error_steps == (a->source_function == root ? 5 : 0));
    }
    return true;
}
TEST_CASE(scoped_native_matches_canonical_vm)
{
    REQUIRE(prepare());
    const behavior_function *functions[] = { scalar, caller };
    const uint64_t budgets[] = { 128, 0, 1, 7 };
    bool native = behavior_backend_available(BEHAVIOR_SLJIT);
    for (int f = 0; f < 2; f++) {
        for (int i = 0; i < 4; i++) {
            behavior_execution_scope vm = { .remaining = budgets[i] };
            behavior_execution_scope jit = { .remaining = budgets[i],
                                             .prefer_compiled = true };
            observation a = run(functions[f], &vm), b = run(functions[f], &jit);
            REQUIRE(parity(functions[f], &a, &b));
            REQUIRE(vm.remaining == jit.remaining);
            REQUIRE(a.counts.bytecode_entries > 0 && !a.counts.sljit_entries);
            REQUIRE(native ? b.counts.sljit_entries > 0 : b.counts.bytecode_entries > 0);
            if (!i && !f) {
                REQUIRE(vm.remaining == 122); /* six canonical operations */
            }
            if (!budgets[i]) {
                REQUIRE(a.status == FLOW_ERR_STEP_BUDGET && a.source == 1);
                REQUIRE(a.has_message && !strcmp(a.message, "shared instruction budget exhausted"));
                REQUIRE(a.node_id == functions[f]->instructions[0].node_id);
            }
            if (f && budgets[i] == 1) {
                REQUIRE(a.source_function == scalar);
            }
        }
        behavior_execution_scope vm = { .remaining = 128, .cancelled = cancelled };
        behavior_execution_scope jit = { .remaining = 128,
                                         .cancelled = cancelled,
                                         .prefer_compiled = true };
        vm.user = &vm;
        jit.user = &jit;
        observation a = run(functions[f], &vm), b = run(functions[f], &jit);
        REQUIRE(parity(functions[f], &a, &b));
        REQUIRE(vm.remaining == 125 && jit.remaining == 125);
        REQUIRE(a.has_message && !strcmp(a.message, "execution cancelled"));
        REQUIRE(a.source_function == scalar);
        REQUIRE(!native || b.counts.sljit_entries > 0);
    }
    return true;
}
TEST_CASE(scoped_native_shared_budget_and_pure_fallback)
{
    REQUIRE(prepare());
    for (int native = 0; native < 2; native++) {
        behavior_execution_scope scope = { .remaining = 7,
                                           .prefer_compiled = native != 0 };
        behavior_execution_scope_begin(&scope);
        observation first = invoke(scalar), second = invoke(scalar);
        behavior_execution_scope_end(&scope);
        REQUIRE(first.status == FLOW_OK && first.value == 42);
        REQUIRE(second.status == FLOW_ERR_STEP_BUDGET && !scope.remaining);
    }
    behavior_execution_scope pure = { .remaining = 128,
                                      .prefer_compiled = true,
                                      .pure = true };
    observation value = run(scalar, &pure);
    REQUIRE(value.status == FLOW_OK && value.value == 42);
    REQUIRE(value.counts.bytecode_entries > 0 && !value.counts.sljit_entries);
    behavior_execution_scope outer = { .remaining = 9, .pure = true };
    behavior_execution_scope inner = { .remaining = 128, .prefer_compiled = true };
    behavior_execution_scope_begin(&outer);
    value = run(scalar, &inner);
    bool restored = behavior_execution_scope_pure();
    behavior_execution_scope_end(&outer);
    REQUIRE(restored);
    REQUIRE(value.status == FLOW_OK && outer.remaining == 9 && inner.remaining == 122);
    return true;
}
static bool package_value_body(behavior_function *body, behavior_function *callee)
{
    execution_module_plan *plan = execution_module_plan_new();
    plan->id = 881001;
    plan->valid = true;
    apush(
        plan->functions,
        ((execution_module_function){ .id = 881002,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .body = body })
    );
    if (callee) {
        apush(
            plan->functions,
            ((execution_module_function){ .id = 881003,
                                          .kind = EXECUTION_FUNCTION_AUTHORED,
                                          .body = callee })
        );
    }
    plan->function_count = acount(plan->functions);
    char error[256];
    value_module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(value_module);
    size_t blob_size;
    REQUIRE(execution_prepared_write(value_module, &value_blob, &blob_size, error, sizeof error));
    package_module row = { "values", 881001, value_blob, blob_size };
    REQUIRE(
        compiler_artifact_write(&row, 1, 881001, &value_artifact, &value_artifact_size, error, sizeof error)
    );
    return true;
}
static bool prepare_value_host(bool random)
{
    behavior_function_builder *b = behavior_function_builder_new();
    pin_value returned = { .type = PIN_I32 };
    behavior_function_builder_signature(b, NULL, 0, &returned, 1, NULL, 0);
    int source = behavior_function_builder_origin(
        b,
        (
            behavior_function_source
        ){ .parent = -1, .asset_name = "values-host.chibi", .line = 7, .column = 3 }
    );
    REQUIRE(source == 1);
    int slot = behavior_function_builder_slot(b, PIN_I32);
    emit(
        b,
        (behavior_function_instruction){ .op = random ? BF_RANDOM : BF_CONSTANT,
                                         .type = PIN_I32,
                                         .output = slot,
                                         .value = { .type = PIN_I32, .i = 7 } }
    );
    emit(b, (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = slot });
    emit(b, (behavior_function_instruction){ .op = BF_RETURN });
    emit(b, (behavior_function_instruction){ .op = BF_END });
    behavior_function *body = finish(b);
    REQUIRE(body);
    REQUIRE(package_value_body(body, NULL));
    char error[256];
    value_host = compiler_host_load_values(
        value_artifact,
        value_artifact_size,
        BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    REQUIRE(value_host);
    return true;
}
static bool poll(void *user)
{
    int *count = user;
    (*count)++;
    return false;
}
static bool cancel_value(void *user)
{
    poll(user);
    return true;
}
static void clear_value_result()
{
    free(value_result.json);
    value_result = (compiler_host_result){ 0 };
}
TEST_CASE(values_host_rejects_nonpure_outer_without_side_effects)
{
    REQUIRE(prepare_value_host(true));
    srand(7);
    int first = rand();
    srand(7);
    REQUIRE(!compiler_host_invoke(value_host, 881002, "[]", 2, 32, &value_result));
    REQUIRE(value_result.flow_status == FLOW_ERR_STEP_BUDGET);
    REQUIRE(strstr(value_result.diagnostic, "operation is unavailable during pure evaluation"));
    REQUIRE(
        value_result.source.present && value_result.source.line == 7 &&
        value_result.source.column == 3
    );
    REQUIRE(rand() == first && !behavior_execution_scope_active());
    clear_value_result();
    for (int compiled = 0; compiled < 2; compiled++) {
        int polls = 0;
        behavior_execution_scope outer = { .remaining = 32,
                                           .prefer_compiled = compiled != 0,
                                           .cancelled = poll,
                                           .user = &polls };
        srand(7);
        behavior_execution_scope_begin(&outer);
        bool invoked = compiler_host_invoke(value_host, 881002, "[]", 2, 32, &value_result);
        bool preserved = behavior_execution_scope_active() &&
                         !behavior_execution_scope_pure() && outer.active &&
                         !outer.previous && outer.remaining == 32 &&
                         outer.cancelled == poll && outer.user == &polls &&
                         !polls && outer.prefer_compiled == (compiled != 0);
        behavior_execution_scope_end(&outer);
        REQUIRE(preserved && !outer.active && !behavior_execution_scope_active());
        REQUIRE(!invoked && !value_result.json && !value_result.length);
        REQUIRE(
            !strcmp(value_result.diagnostic, "values-only invocation requires a pure execution scope")
        );
        REQUIRE(
            !value_result.counts.bytecode_entries &&
            !value_result.counts.sljit_entries && !value_result.counts.wasm_entries &&
            !value_result.counts.inspected_sljit_entries &&
            !value_result.counts.inspected_wasm_entries &&
            !value_result.counts.inspection_bytecode_fallbacks &&
            !value_result.counts.size_bytecode_fallbacks && !value_result.counts.overflow
        );
        REQUIRE(rand() == first);
        clear_value_result();
    }
    return true;
}
TEST_CASE(values_host_preserves_pure_outer_budget_and_cancellation)
{
    REQUIRE(prepare_value_host(false));
    size_t retained = compiler_host_storage_bytes(value_host);
    REQUIRE(retained > 0 && retained != SIZE_MAX);
    REQUIRE(compiler_host_invoke(value_host, 881002, "[]", 2, 32, &value_result));
    REQUIRE(value_result.flow_status == FLOW_OK && !strcmp(value_result.json, "[7]"));
    REQUIRE(!behavior_execution_scope_active());
    clear_value_result();
    int polls = 0;
    behavior_execution_scope outer = { .remaining = 32,
                                       .pure = true,
                                       .prefer_compiled = true,
                                       .cancelled = poll,
                                       .user = &polls };
    behavior_execution_scope_begin(&outer);
    bool invoked = compiler_host_invoke(value_host, 881002, "[]", 2, 32, &value_result);
    bool preserved = behavior_execution_scope_pure() && outer.active &&
                     outer.remaining == 29 && polls == 3;
    behavior_execution_scope_end(&outer);
    REQUIRE(preserved && invoked && !strcmp(value_result.json, "[7]"));
    REQUIRE(value_result.counts.bytecode_entries > 0 && !value_result.counts.sljit_entries);
    REQUIRE(compiler_host_storage_bytes(value_host) == retained);
    clear_value_result();
    polls = 0;
    outer.remaining = 32;
    outer.cancelled = cancel_value;
    behavior_execution_scope_begin(&outer);
    invoked = compiler_host_invoke(value_host, 881002, "[]", 2, 32, &value_result);
    preserved = behavior_execution_scope_pure() && outer.active &&
                outer.remaining == 32 && polls == 1;
    behavior_execution_scope_end(&outer);
    REQUIRE(preserved && !invoked && !value_result.json && !value_result.length);
    REQUIRE(
        value_result.flow_status == FLOW_ERR_STEP_BUDGET &&
        strstr(value_result.diagnostic, "execution cancelled")
    );
    REQUIRE(!behavior_execution_scope_active());
    return true;
}
TEST_CASE(values_host_target_facts_and_entry_queries_match_profile)
{
    REQUIRE(prepare_value_host(false));
    char error[256];
    size_t actual_size, expected_size;
    char *actual = compiler_host_target_json(value_host, &actual_size);
    char *expected = compiler_host_values_target_json(&expected_size, error, sizeof error);
    bool same = actual && expected && actual_size == expected_size &&
                !strcmp(actual, expected);
    free(actual);
    free(expected);
    REQUIRE(same);
    REQUIRE(!compiler_host_accepts_byte_entry(value_host, 881002, 0, error, sizeof error));
    REQUIRE(strstr(error, "incompatible signature"));
    REQUIRE(!compiler_host_accepts_byte_entry(value_host, 881002, -1, error, sizeof error));
    REQUIRE(!compiler_host_accepts_byte_entry(value_host, 881003, 0, error, sizeof error));
    compiler_host *full = compiler_host_load(
        value_artifact,
        value_artifact_size,
        BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    REQUIRE(full);
    actual = compiler_host_target_json(full, &actual_size);
    expected = compiler_host_compiler_target_json(&expected_size, error, sizeof error);
    same = actual && expected && actual_size == expected_size &&
           !strcmp(actual, expected) &&
           compiler_host_storage_bytes(full) >= compiler_host_storage_bytes(value_host);
    free(actual);
    free(expected);
    compiler_host_free(full);
    REQUIRE(same);
    return true;
}
TEST_CASE(values_host_byte_entry_query_requires_exact_arity)
{
    behavior_function_builder *b = behavior_function_builder_new();
    pin_value bytes = { .type = PIN_U8, .container = PIN_CONTAINER_ARRAY };
    behavior_function_builder_signature(b, &bytes, 1, &bytes, 1, NULL, 0);
    behavior_function_builder_emit(b, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(b, (behavior_function_instruction){ .op = BF_END });
    behavior_function *body = finish(b);
    REQUIRE(body && package_value_body(body, NULL));
    char error[256];
    value_host = compiler_host_load_values(
        value_artifact,
        value_artifact_size,
        BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    REQUIRE(value_host);
    REQUIRE(compiler_host_accepts_byte_entry(value_host, 881002, 1, error, sizeof error));
    REQUIRE(!compiler_host_accepts_byte_entry(value_host, 881002, 0, error, sizeof error));
    REQUIRE(!compiler_host_accepts_byte_entry(value_host, 881002, 2, error, sizeof error));
    return true;
}
TEST_CASE(values_host_rejects_foreign_compiler_bridge)
{
    char error[256];
    value_native = native_capabilities_from_definition_packs(
        NULL,
        0,
        &COMPILER_JSON_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
    REQUIRE(value_native);
    uint64_t id = script_catalog_id("capability", "Compiler JSON String");
    const native_capability_binding *binding = native_capabilities_find(value_native, id, 0);
    REQUIRE(binding && binding->call && binding->access && binding->abi);
    behavior_function_builder *b = behavior_function_builder_new();
    pin_value number = { .type = PIN_I32 }, input = { .type = PIN_STR };
    pin_value outputs[] = { { .type = PIN_BOOL }, { .type = PIN_STR }, { .type = PIN_I32 } };
    behavior_function_builder_signature(b, NULL, 0, &number, 1, NULL, 0);
    int call = behavior_function_builder_host(b, binding->call, binding->access, &input, 1, outputs, 3);
    REQUIRE(
        behavior_function_builder_host_identity(b, call, binding->id, binding->abi)
    );
    int slot = behavior_function_builder_slot(b, PIN_I32);
    behavior_function_builder_emit(
        b,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .target = call,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA }
    );
    behavior_function_builder_emit(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .output = slot,
                                         .value = { .type = PIN_I32, .i = 7 } }
    );
    behavior_function_builder_emit(
        b,
        (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_I32, .left = slot }
    );
    behavior_function_builder_emit(b, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(b, (behavior_function_instruction){ .op = BF_END });
    behavior_function *body = finish(b);
    REQUIRE(body && package_value_body(body, NULL));
    value_host = compiler_host_load(value_artifact, value_artifact_size, BEHAVIOR_BYTECODE, error, sizeof error);
    REQUIRE(value_host);
    REQUIRE(compiler_host_invoke(value_host, 881002, "[]", 2, 32, &value_result));
    REQUIRE(value_result.json && !strcmp(value_result.json, "[7]"));
    clear_value_result();
    compiler_host_free(value_host);
    value_host = compiler_host_load_values(
        value_artifact,
        value_artifact_size,
        BEHAVIOR_BYTECODE,
        error,
        sizeof error
    );
    REQUIRE(!value_host && strstr(error, "missing or incompatible host capability"));
    return true;
}
TEST_CASE(scoped_pure_rejects_task_before_suspension)
{
    REQUIRE(prepare());
    behavior_function_free(caller);
    caller = NULL;
    behavior_function_builder *b = builder(false);
    pin_value argument = { .type = PIN_F64, .d = 21 };
    int call = behavior_function_builder_task(b, scalar, &argument, 1);
    int slot = behavior_function_builder_slot(b, PIN_F64);
    emit(b, (behavior_function_instruction){ .op = BF_INVOKE, .left = call });
    emit(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .output = slot,
                                         .value = { .type = PIN_F64, .d = 42 } }
    );
    emit(b, (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = slot });
    emit(b, (behavior_function_instruction){ .op = BF_RETURN });
    emit(b, (behavior_function_instruction){ .op = BF_END });
    caller = finish(b);
    REQUIRE(caller);
    for (int compiled = 0; compiled < 2; compiled++) {
        task_starts = 0;
        behavior_execution_scope scope = { .remaining = 128,
                                           .pure = true,
                                           .prefer_compiled = compiled != 0 };
        observation value = run(caller, &scope);
        /* The old path reaches a nonnull sentinel, never a NULL callback.
         * Fail here before the actual values-host check below on old code. */
        REQUIRE(!task_starts && !value.pc);
        REQUIRE(value.status == FLOW_ERR_STEP_BUDGET && !value.returned);
        REQUIRE(
            value.has_message &&
            !strcmp(value.message, "operation is unavailable during pure evaluation")
        );
        REQUIRE(value.source_function == caller && value.source == 1);
        REQUIRE(value.node_id == caller->instructions[0].node_id && value.error_pin == -1);
        REQUIRE(value.steps == 5 && value.error_steps == 5);
        REQUIRE(scope.remaining == 127 && !behavior_execution_scope_active());
        REQUIRE(value.counts.bytecode_entries == 1 && !value.counts.sljit_entries);

        scope = (behavior_execution_scope){ .remaining = 128,
                                            .prefer_compiled = compiled != 0 };
        value = run(caller, &scope);
        REQUIRE(task_starts == 1 && value.pc > 0);
        REQUIRE(value.status == FLOW_SUSPENDED_RESULT && scope.remaining == 127);
        REQUIRE(!behavior_execution_scope_active());
    }
    behavior_function *body = caller, *callee = scalar;
    caller = scalar = NULL;
    REQUIRE(package_value_body(body, callee));
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int backend = 0; backend < 2; backend++) {
        if (!behavior_backend_available(backends[backend])) {
            continue;
        }
        char error[256];
        value_host = compiler_host_load_values(
            value_artifact,
            value_artifact_size,
            backends[backend],
            error,
            sizeof error
        );
        REQUIRE(value_host);
        REQUIRE(!compiler_host_invoke(value_host, 881002, "[]", 2, 128, &value_result));
        REQUIRE(
            value_result.flow_status == FLOW_ERR_STEP_BUDGET &&
            !value_result.json && !value_result.length
        );
        REQUIRE(strstr(value_result.diagnostic, "operation is unavailable during pure evaluation"));
        REQUIRE(
            value_result.source.present &&
            !strcmp(value_result.source.asset, "execution-scope.chibi")
        );
        REQUIRE(value_result.source.line == 7 && value_result.source.column == 3);
        REQUIRE(
            value_result.counts.bytecode_entries == 1 && !value_result.counts.sljit_entries
        );
        REQUIRE(!behavior_execution_scope_active());
        clear_value_result();
        compiler_host_free(value_host);
        value_host = NULL;
    }
    return true;
}
void test_language_execution_scope()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(scoped_native_matches_canonical_vm);
    RUN_TEST_CASE(scoped_native_shared_budget_and_pure_fallback);
    RUN_TEST_CASE(values_host_rejects_nonpure_outer_without_side_effects);
    RUN_TEST_CASE(values_host_preserves_pure_outer_budget_and_cancellation);
    RUN_TEST_CASE(values_host_target_facts_and_entry_queries_match_profile);
    RUN_TEST_CASE(values_host_byte_entry_query_requires_exact_arity);
    RUN_TEST_CASE(values_host_rejects_foreign_compiler_bridge);
    RUN_TEST_CASE(scoped_pure_rejects_task_before_suspension);
    pu_clear_setup();
}
