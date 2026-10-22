#include "execution/backend.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/status.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* No source compiler, resources, workers or I/O. Every invocation is capped at
 * 128 instructions; the complete selector makes at most 40 invocations. */
static behavior_function *scalar, *caller;
static void cleanup()
{
    behavior_function_free(caller);
    behavior_function_free(scalar);
    caller = scalar = NULL;
}
static void emit(behavior_function_builder *b, behavior_function_instruction instruction)
{
    instruction.source = 1;
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
    int status, source, frame, stack;
    double value;
    bool returned;
    const char *message;
    behavior_run_counts counts;
} observation;
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
                                          .activation = resumable ? &activation : NULL };
    behavior_function_bind_storage(&context, &storage);
    assert(behavior_function_bind(f, &context, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(f, &context, false);
    behavior_run_scope_end(&runs);
    observation out = { .status = status,
                        .source = context.error_source,
                        .frame = context.error_frame,
                        .stack = context.error_stack,
                        .value = result.d,
                        .returned = context.returned,
                        .message = context.error_message,
                        .counts = runs.counts };
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
static bool parity(const observation *a, const observation *b)
{
    REQUIRE(a->status == b->status);
    REQUIRE(a->returned == b->returned);
    REQUIRE(a->source == b->source && a->frame == b->frame && a->stack == b->stack);
    REQUIRE(!a->message == !b->message);
    REQUIRE(!a->message || !strcmp(a->message, b->message));
    if (a->status == FLOW_OK) {
        REQUIRE(a->returned && a->value == 42 && b->value == 42);
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
            REQUIRE(parity(&a, &b));
            REQUIRE(vm.remaining == jit.remaining);
            REQUIRE(a.counts.bytecode_entries > 0 && !a.counts.sljit_entries);
            REQUIRE(native ? b.counts.sljit_entries > 0 : b.counts.bytecode_entries > 0);
            if (!i && !f) {
                REQUIRE(vm.remaining == 122); /* six canonical operations */
            }
            if (!budgets[i]) {
                REQUIRE(a.status == FLOW_ERR_STEP_BUDGET && a.source == 1);
                REQUIRE(a.message && !strcmp(a.message, "shared instruction budget exhausted"));
            }
        }
        behavior_execution_scope vm = { .remaining = 128, .cancelled = cancelled };
        behavior_execution_scope jit = { .remaining = 128,
                                         .cancelled = cancelled,
                                         .prefer_compiled = true };
        vm.user = &vm;
        jit.user = &jit;
        observation a = run(functions[f], &vm), b = run(functions[f], &jit);
        REQUIRE(parity(&a, &b));
        REQUIRE(vm.remaining == 125 && jit.remaining == 125);
        REQUIRE(a.message && !strcmp(a.message, "execution cancelled"));
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
void test_language_execution_scope()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(scoped_native_matches_canonical_vm);
    RUN_TEST_CASE(scoped_native_shared_budget_and_pure_fallback);
    pu_clear_setup();
}
