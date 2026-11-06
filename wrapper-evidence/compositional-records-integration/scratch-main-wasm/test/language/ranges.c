#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/status.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

static behavior_function_builder *builder;
static behavior_function *function;
static behavior_function_context context;
static arena storage;

static void cleanup()
{
    behavior_function_release_host(&context);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    if (builder) {
        behavior_function_builder_free(builder);
    }
    behavior_function_free(function);
    builder = NULL;
    function = NULL;
}
static int emit(behavior_function_instruction i)
{
    return behavior_function_builder_emit(builder, i);
}
static int operation(behavior_function_op op, pin_type type, int left, int right)
{
    int output = behavior_function_builder_slot(builder, type);
    if (op >= BF_LT && op <= BF_NE) {
        type = behavior_function_builder_definition(builder)->slots[left];
    }
    emit((behavior_function_instruction){ .op = op,
                                          .type = type,
                                          .output = output,
                                          .left = left,
                                          .right = right,
                                          .value = { .type = type } });
    return output;
}
static int number(pin_type type, double value)
{
    int output = behavior_function_builder_slot(builder, type);
    pin_value v = { .type = type };
    if (type == PIN_F32) {
        v.f = (float)value;
    } else {
        v.d = value;
    }
    emit((
        behavior_function_instruction
    ){ .op = BF_CONSTANT, .type = type, .output = output, .value = v });
    return output;
}
static void result(pin_type type, int value, int target)
{
    emit((
        behavior_function_instruction
    ){ .op = BF_RESULT, .type = type, .left = value, .target = target });
}
static bool finish()
{
    emit((behavior_function_instruction){ .op = BF_RETURN });
    emit((behavior_function_instruction){ .op = BF_END });
    char error[256];
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "numeric ranges: %s\n", error);
    }
    REQUIRE(function && behavior_function_compile(function));
#ifdef __EMSCRIPTEN__
    REQUIRE(!behavior_function_compile_inspection(function));
#else
    REQUIRE(behavior_function_compile_inspection(function));
#endif
    return true;
}
static int inspect(void *user, const behavior_function_point *point)
{
    (void)user;
    (void)point;
    return 0;
}
static bool run(const pin_value *arguments, int count, pin_value *results, int mode)
{
    behavior_function_release_host(&context);
    arena_free(&storage);
    context = (behavior_function_context){ .arguments = arguments,
                                           .argument_count = count,
                                           .results = results,
                                           .budget = 1024,
                                           .inspect = mode & 2 ? inspect : NULL };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    REQUIRE(behavior_function_run(function, &context, mode & 1) == FLOW_OK);
    return true;
}
static int operations(behavior_function_op op, bool optimized)
{
    const behavior_function_instruction *code = optimized
                                                  ? function->runtime_instructions
                                                  : function->instructions;
    int count = 0;
    for (int i = 0; i < acount(code); i++) {
        count += code[i].op == op;
    }
    return count;
}
static bool equal(double actual, double expected)
{
    REQUIRE(isnan(expected) ? isnan(actual) : actual == expected);
    REQUIRE(expected != 0 || !!signbit(actual) == !!signbit(expected));
    return true;
}
TEST_CASE(proven_intervals_remove_repeated_clamps_and_checks)
{
    const double samples[] = { NAN, -INFINITY, INFINITY, -0., 0, 1, 3.25, 10, 11 };
    for (int width = 0; width < 2; width++) {
        cleanup();
        pin_type type = width ? PIN_F64 : PIN_F32;
        pin_value argument = { .type = type };
        pin_value outputs[] = { { .type = type }, { .type = PIN_BOOL } };
        builder = behavior_function_builder_new();
        behavior_function_builder_signature(builder, &argument, 1, outputs, 2, NULL, 0);
        int input = operation(BF_ARGUMENT, type, 0, 0);
        int low = number(type, 1), high = number(type, 10);
        int first = operation(BF_MAX, type, operation(BF_MIN, type, input, high), low);
        int second = operation(BF_MAX, type, operation(BF_MIN, type, first, high), low);
        result(type, second, 0);
        result(PIN_BOOL, operation(BF_LT, PIN_BOOL, second, number(type, 20)), 1);
        REQUIRE(finish());
        REQUIRE(operations(BF_MIN, false) == 2 && operations(BF_MAX, false) == 2);
        REQUIRE(operations(BF_MIN, true) == 1 && operations(BF_MAX, true) == 1);
        REQUIRE(operations(BF_LT, true) == 0);
        for (int sample = 0; sample < (int)(sizeof samples / sizeof *samples);
             sample++) {
            double x = samples[sample];
            double expected = x < 10 ? x : 10;
            expected = expected > 1 ? expected : 1;
            if (width) {
                argument.d = x;
            } else {
                argument.f = (float)x;
            }
            for (int mode = 0; mode < 4; mode++) {
                REQUIRE(run(&argument, 1, outputs, mode));
                REQUIRE(equal(width ? outputs[0].d : outputs[0].f, expected));
                REQUIRE(outputs[1].b);
            }
        }
    }
    return true;
}
TEST_CASE(range_clamps_preserve_unordered_and_signed_zero_selection)
{
    const double samples[] = { NAN, -INFINITY, INFINITY, -0., 0., -.5, .5 };
    builder = behavior_function_builder_new();
    pin_value argument = { .type = PIN_F64 };
    pin_value outputs[] = { { .type = PIN_F64 }, { .type = PIN_F64 }, { .type = PIN_BOOL } };
    behavior_function_builder_signature(builder, &argument, 1, outputs, 3, NULL, 0);
    int input = operation(BF_ARGUMENT, PIN_F64, 0, 0);
    int negative = number(PIN_F64, -0.), positive = number(PIN_F64, 0.);
    int first = operation(BF_MIN, PIN_F64, input, negative);
    int second = operation(BF_MIN, PIN_F64, first, positive);
    int unordered = operation(BF_MIN, PIN_F64, input, number(PIN_F64, NAN));
    result(PIN_F64, second, 0);
    result(PIN_F64, unordered, 1);
    result(PIN_BOOL, operation(BF_EQ, PIN_BOOL, unordered, unordered), 2);
    REQUIRE(finish());
    REQUIRE(operations(BF_EQ, true) == 1);
    for (int sample = 0; sample < (int)(sizeof samples / sizeof *samples); sample++) {
        argument.d = samples[sample];
        double first_value = argument.d < -0. ? argument.d : -0.;
        double expected = first_value < 0. ? first_value : 0.;
        for (int mode = 0; mode < 4; mode++) {
            REQUIRE(run(&argument, 1, outputs, mode));
            REQUIRE(equal(outputs[0].d, expected));
            REQUIRE(isnan(outputs[1].d) && !outputs[2].b);
        }
    }
    return true;
}
TEST_CASE(interval_arithmetic_keeps_ieee_rounding_and_overflow)
{
    builder = behavior_function_builder_new();
    pin_value argument = { .type = PIN_F64 };
    pin_value outputs[] = { { .type = PIN_F64 }, { .type = PIN_F64 }, { .type = PIN_BOOL } };
    behavior_function_builder_signature(builder, &argument, 1, outputs, 3, NULL, 0);
    int input = operation(BF_ARGUMENT, PIN_F64, 0, 0);
    int clamped = operation(
        BF_MAX,
        PIN_F64,
        operation(BF_MIN, PIN_F64, input, number(PIN_F64, 10)),
        number(PIN_F64, 1)
    );
    int big = number(PIN_F64, 0x1p53);
    int sum = operation(BF_ADD, PIN_F64, big, number(PIN_F64, 1));
    int maximum = number(PIN_F64, DBL_MAX);
    int product = operation(BF_MUL, PIN_F64, clamped, maximum);
    result(PIN_F64, operation(BF_SUB, PIN_F64, sum, big), 0);
    result(PIN_F64, product, 1);
    result(PIN_BOOL, operation(BF_LE, PIN_BOOL, product, maximum), 2);
    REQUIRE(finish());
    REQUIRE(operations(BF_ADD, true) == 1 && operations(BF_SUB, true) == 1);
    REQUIRE(operations(BF_LE, true) == 1);
    for (int i = 1; i <= 2; i++) {
        argument.d = i;
        for (int mode = 0; mode < 4; mode++) {
            REQUIRE(run(&argument, 1, outputs, mode));
            REQUIRE(outputs[0].d == 0);
            REQUIRE(equal(outputs[1].d, i == 1 ? DBL_MAX : INFINITY));
            REQUIRE(outputs[2].b == (i == 1));
        }
    }
    return true;
}
TEST_CASE(exact_integer_comparisons_avoid_number_round_trips)
{
    for (int kind = 0; kind < 3; kind++) {
        cleanup();
        pin_type type = kind == 0 ? PIN_I32 : kind == 1 ? PIN_U32 : PIN_I64;
        pin_value inputs[] = { { .type = type }, { .type = type } };
        pin_value output = { .type = PIN_BOOL };
        builder = behavior_function_builder_new();
        behavior_function_builder_signature(builder, inputs, 2, &output, 1, NULL, 0);
        int left = operation(BF_CAST, PIN_F64, operation(BF_ARGUMENT, type, 0, 0), 0);
        int right = operation(BF_CAST, PIN_F64, operation(BF_ARGUMENT, type, 1, 0), 0);
        result(PIN_BOOL, operation(kind == 0 ? BF_LT : BF_EQ, PIN_BOOL, left, right), 0);
        REQUIRE(finish());
        REQUIRE(operations(BF_CAST, true) == (kind == 2 ? 2 : 0));
        for (int i = 0; i < 3; i++) {
            bool expected;
            if (kind == 0) {
                inputs[0].i = i == 0 ? INT32_MIN : i == 1 ? INT32_MAX : -1;
                inputs[1].i = i == 0 ? INT32_MAX : i == 1 ? INT32_MIN : 0;
                expected = inputs[0].i < inputs[1].i;
            } else if (kind == 1) {
                inputs[0].u = i == 0 ? UINT32_MAX
                            : i == 1 ? 0
                                     : UINT32_C(0x80000000);
                inputs[1].u = i == 0 ? UINT32_MAX : i == 1 ? UINT32_MAX : 0;
                expected = inputs[0].u == inputs[1].u;
            } else {
                inputs[0].i64 = INT64_C(9007199254740992) + i;
                inputs[1].i64 = INT64_C(9007199254740992);
                expected = (double)inputs[0].i64 == (double)inputs[1].i64;
            }
            for (int mode = 0; mode < 4; mode++) {
                REQUIRE(run(inputs, 2, &output, mode));
                REQUIRE(output.b == expected);
            }
        }
    }
    return true;
}
TEST_CASE(reassigned_integer_operands_do_not_change_previous_numbers)
{
    for (int scenario = 0; scenario < 10; scenario++) {
        cleanup();
        bool aliased = scenario >= 6;
        int secondary = aliased ? 1 + scenario % 2 : scenario % 3;
        pin_type type = scenario < 3 || (scenario >= 6 && scenario < 8) ? PIN_I32
                                                                        : PIN_I64;
        builder = behavior_function_builder_new();
        pin_value inputs[] = { { .type = type }, { .type = type } };
        pin_value divisor_value = { .type = type };
        if (type == PIN_I32) {
            inputs[0].i = 10;
            inputs[1].i = 5;
            divisor_value.i = 2;
        } else {
            inputs[0].i64 = 10;
            inputs[1].i64 = 5;
            divisor_value.i64 = 2;
        }
        pin_value outputs[] = { { .type = PIN_BOOL }, { .type = type }, { .type = type } };
        behavior_function_builder_signature(builder, inputs, 2, outputs, aliased ? 3 : 2, NULL, 0);
        int local = -1;
        if (aliased) {
            local = behavior_function_builder_local(builder, inputs[0]);
            emit((behavior_function_instruction){ .op = BF_CHECK });
        }
        int integer = operation(aliased ? BF_LOCAL : BF_ARGUMENT, type, aliased ? local : 0, 0);
        int other = operation(BF_ARGUMENT, type, 1, 0);
        int left = operation(BF_CAST, PIN_F64, integer, 0);
        if (secondary) {
            int divisor = behavior_function_builder_slot(builder, type);
            emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                                  .type = type,
                                                  .output = divisor,
                                                  .value = divisor_value });
            emit((behavior_function_instruction){
                .op = secondary == 1 ? BF_DIV : BF_MOD,
                .type = type,
                .output = behavior_function_builder_slot(builder, type),
                .left = other,
                .right = divisor,
                .target = integer });
        } else {
            emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                                  .type = type,
                                                  .output = integer,
                                                  .value = { .type = type } });
        }
        int right = operation(BF_CAST, PIN_F64, other, 0);
        result(PIN_BOOL, operation(BF_LT, PIN_BOOL, left, right), 0);
        /* Keep the later definition observable. Otherwise dead-value removal
         * can erase the overwrite and safely compare the original integers. */
        result(type, integer, 1);
        if (aliased) {
            result(type, operation(BF_LOCAL, type, local, 0), 2);
            /* A statically present backedge lets the JIT retain the local in
             * a saved register. Execute the body once; its source local must
             * survive overwriting the separately loaded scalar slot. */
            int done = operation(BF_CONSTANT, PIN_BOOL, 0, 0);
            emit((behavior_function_instruction){ .op = BF_BRANCH,
                                                  .left = done,
                                                  .target = 0 });
        }
        REQUIRE(finish());
        REQUIRE(operations(BF_CAST, true) == 2);
        for (int mode = 0; mode < 4; mode++) {
            REQUIRE(run(inputs, 2, outputs, mode));
            REQUIRE(!outputs[0].b);
            int64_t actual = type == PIN_I32 ? outputs[1].i : outputs[1].i64;
            if (actual != secondary) {
                fprintf(
                    stderr,
                    "range overwrite: scenario=%d mode=%d actual=%lld "
                    "expected=%d\n",
                    scenario,
                    mode,
                    (long long)actual,
                    secondary
                );
            }
            REQUIRE(actual == secondary);
            if (aliased) {
                REQUIRE((type == PIN_I32 ? outputs[2].i : outputs[2].i64) == 10);
            }
        }
    }
    return true;
}
TEST_CASE(control_joins_discard_path_specific_intervals)
{
    builder = behavior_function_builder_new();
    pin_value inputs[] = { { .type = PIN_F64, .d = 100 }, { .type = PIN_BOOL } };
    pin_value output = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, inputs, 2, &output, 1, NULL, 0);
    int input = operation(BF_ARGUMENT, PIN_F64, 0, 0);
    int high = number(PIN_F64, 10), twenty = number(PIN_F64, 20);
    int clamped = operation(BF_MIN, PIN_F64, input, high);
    int condition = operation(BF_ARGUMENT, PIN_BOOL, 1, 0);
    int branch = emit((behavior_function_instruction){ .op = BF_BRANCH,
                                                       .left = condition });
    emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                          .type = PIN_F64,
                                          .output = clamped,
                                          .value = { .type = PIN_F64 } });
    int join = behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){
            .op = BF_LT,
            .type = PIN_F64,
            .output = behavior_function_builder_slot(builder, PIN_BOOL),
            .left = clamped,
            .right = twenty }
    );
    REQUIRE(behavior_function_builder_patch_target(builder, branch, join));
    const behavior_function *draft = behavior_function_builder_definition(builder);
    result(PIN_BOOL, draft->instructions[join].output, 0);
    REQUIRE(finish());
    REQUIRE(operations(BF_LT, true) == 1);
    for (int condition_value = 0; condition_value < 2; condition_value++) {
        inputs[1].b = condition_value;
        for (int mode = 0; mode < 4; mode++) {
            REQUIRE(run(inputs, 2, &output, mode));
            REQUIRE(output.b == inputs[1].b);
        }
    }
    return true;
}
static const char *opaque(void *service, const pin_value *arguments, pin_value *results)
{
    (void)service;
    (void)arguments;
    (void)results;
    return NULL;
}
TEST_CASE(opaque_calls_end_numeric_proofs)
{
    builder = behavior_function_builder_new();
    pin_value input = { .type = PIN_F64, .d = 100 }, output = { .type = PIN_BOOL };
    behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
    int argument = operation(BF_ARGUMENT, PIN_F64, 0, 0);
    int high = number(PIN_F64, 10), twenty = number(PIN_F64, 20);
    int clamped = operation(BF_MIN, PIN_F64, argument, high);
    static const node_host_call call = { .invoke = opaque };
    static const node_access access = { 0 };
    int host = behavior_function_builder_host(builder, &call, &access, NULL, 0, NULL, 0);
    emit((behavior_function_instruction){ .op = BF_HOST_CALL,
                                          .left = BEHAVIOR_FUNCTION_HOST_DATA,
                                          .target = host });
    result(PIN_BOOL, operation(BF_LT, PIN_BOOL, clamped, twenty), 0);
    REQUIRE(finish());
    REQUIRE(operations(BF_LT, true) == 1);
    for (int mode = 0; mode < 4; mode++) {
        REQUIRE(run(&input, 1, &output, mode));
        REQUIRE(output.b);
    }
    return true;
}
void test_numeric_ranges()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(proven_intervals_remove_repeated_clamps_and_checks);
    RUN_TEST_CASE(range_clamps_preserve_unordered_and_signed_zero_selection);
    RUN_TEST_CASE(interval_arithmetic_keeps_ieee_rounding_and_overflow);
    RUN_TEST_CASE(exact_integer_comparisons_avoid_number_round_trips);
    RUN_TEST_CASE(reassigned_integer_operands_do_not_change_previous_numbers);
    RUN_TEST_CASE(control_joins_discard_path_specific_intervals);
    RUN_TEST_CASE(opaque_calls_end_numeric_proofs);
    pu_clear_setup();
}
