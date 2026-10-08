#include "execution/function_builder.h"
#include "execution/status.h"
#include "runtime/arithmetic.h"
#include "support/cos_sin.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdio.h>

static behavior_function_builder *builder;
static behavior_function *function;
static arena storage;
static void cleanup()
{
    arena_free(&storage);
    if (builder) {
        behavior_function_builder_free(builder);
    }
    if (function) {
        behavior_function_free(function);
    }
    builder = NULL;
    function = NULL;
}
static int emit(behavior_function_op op, pin_type type, int output, int left, int right)
{
    return behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = op,
                                         .type = type,
                                         .output = output,
                                         .left = left,
                                         .right = right,
                                         .target = op == BF_RESULT ? output : 0,
                                         .value = { .type = type } }
    );
}
static bool finish()
{
    char error[256];
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "primitive math: %s\n", error);
    }
    REQUIRE(function && behavior_function_compile(function));
    return true;
}
static bool run(const pin_value *arguments, int count, pin_value *variables, pin_value *results, bool native)
{
    arena_free(&storage);
    behavior_function_context context = { .arguments = arguments,
                                          .argument_count = count,
                                          .variables = variables,
                                          .results = results,
                                          .budget = 1024 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    REQUIRE(behavior_function_run(function, &context, native) == FLOW_OK);
    return true;
}
static bool same_real(double actual, double expected)
{
    REQUIRE(isnan(expected) ? isnan(actual) : actual == expected);
    REQUIRE(expected != 0 || signbit(actual) == signbit(expected));
    return true;
}
static double reference(behavior_function_op op, const pin_value *args, pin_type type)
{
    double x = type == PIN_F32 ? args[0].f : args[0].d;
    double y = type == PIN_F32 ? args[1].f : args[1].d;
    switch (op) {
        case BF_SIN: return cos_sin_sine(x);
        case BF_COS: return cos_sin_cosine(x);
        case BF_TAN: return tan(x);
        case BF_ASIN: return asin(x);
        case BF_ACOS: return acos(x);
        case BF_ATAN: return atan(x);
        case BF_EXP: return exp(x);
        case BF_LOG: return log(x);
        case BF_LOG10: return log10(x);
        case BF_POW: return pow(x, y);
        case BF_ATAN2: return atan2(x, y);
        case BF_MIN: {
            arena temporary = { 0 };
            pin_value value = native_arithmetic_clamp(args, 3, &temporary);
            double result = type == PIN_F32 ? value.f : value.d;
            arena_free(&temporary);
            return result;
        }
        default: return NAN;
    }
}
static bool primitive(behavior_function_op op, pin_type type, int count)
{
    builder = behavior_function_builder_new();
    pin_value args[3] = { { .type = type }, { .type = type }, { .type = type } };
    pin_value result = { .type = type };
    behavior_function_builder_signature(builder, args, count, &result, 1, NULL, 0);
    int slots[3];
    for (int i = 0; i < count; i++) {
        slots[i] = behavior_function_builder_slot(builder, type);
        emit(BF_ARGUMENT, type, slots[i], i, 0);
    }
    int output = behavior_function_builder_slot(builder, type);
    if (count == 3) {
        int bounded = behavior_function_builder_slot(builder, type);
        emit(BF_MIN, type, bounded, slots[0], slots[2]);
        emit(BF_MAX, type, output, bounded, slots[1]);
    } else {
        emit(op, type, output, slots[0], count == 2 ? slots[1] : 0);
    }
    emit(BF_RESULT, type, 0, output, 0);
    emit(BF_RETURN, PIN_I32, 0, 0, 0);
    emit(BF_END, PIN_I32, 0, 0, 0);
    REQUIRE(finish());
    const double cases[] = { NAN, INFINITY, -INFINITY, -0.,   0.,
                             -1., 1.,       1.e-40,    1.e20, .375 };
    int samples = count == 3 ? 1000 : count == 2 ? 100 : 10;
    for (int sample = 0; sample < samples; sample++) {
        int index = sample;
        for (int i = 0; i < count; i++) {
            if (type == PIN_F32) {
                args[i].f = (float)cases[index % 10];
            } else {
                args[i].d = cases[index % 10];
            }
            index /= 10;
        }
        double expected = reference(op, args, type);
        for (int native = 0; native < 2; native++) {
            result = (pin_value){ .type = type };
            REQUIRE(run(args, count, NULL, &result, native));
            REQUIRE(result.type == type);
            REQUIRE(same_real(type == PIN_F32 ? result.f : result.d, expected));
        }
    }
    cleanup();
    return true;
}
TEST_CASE(public_math_edge_values_match_native_semantics)
{
    const behavior_function_op operations[] = { BF_SIN,  BF_COS,   BF_TAN,
                                                BF_ASIN, BF_ACOS,  BF_ATAN,
                                                BF_EXP,  BF_LOG,   BF_LOG10,
                                                BF_POW,  BF_ATAN2, BF_MIN };
    for (int i = 0; i < 12; i++) {
        REQUIRE(primitive(
            operations[i],
            PIN_F64,
            i == 11  ? 3
            : i >= 9 ? 2
                     : 1
        ));
    }
    REQUIRE(primitive(BF_MIN, PIN_F32, 3));
    return true;
}
TEST_CASE(public_math_calls_preserve_live_registers_and_state)
{
    builder = behavior_function_builder_new();
    pin_value args[8],
        results[3] = { { .type = PIN_F64 }, { .type = PIN_F32 }, { .type = PIN_BOOL } };
    for (int i = 0; i < 8; i++) {
        args[i] = (pin_value){ .type = i < 6  ? PIN_F64
                                     : i == 6 ? PIN_F32
                                              : PIN_BOOL };
    }
    pin_value bias = { .type = PIN_F64, .d = 2.75 };
    behavior_function_builder_signature(builder, args, 8, results, 3, &bias, 1);
    int argument[8], sine[6], cosine[6];
    for (int i = 0; i < 8; i++) {
        argument[i] = behavior_function_builder_slot(builder, args[i].type);
        emit(BF_ARGUMENT, args[i].type, argument[i], i, 0);
    }
    for (int i = 0; i < 6; i++) {
        sine[i] = behavior_function_builder_slot(builder, PIN_F64);
        emit(BF_SIN, PIN_F64, sine[i], argument[i], 0);
    }
    /* All six sine results remain live across the following C math calls. */
    for (int i = 0; i < 6; i++) {
        cosine[i] = behavior_function_builder_slot(builder, PIN_F64);
        emit(BF_COS, PIN_F64, cosine[i], sine[i], 0);
    }
    int sum = sine[0], cos_sum = cosine[0];
    for (int i = 1; i < 6; i++) {
        int next = behavior_function_builder_slot(builder, PIN_F64);
        emit(BF_ADD, PIN_F64, next, sum, sine[i]);
        sum = next;
        next = behavior_function_builder_slot(builder, PIN_F64);
        emit(BF_ADD, PIN_F64, next, cos_sum, cosine[i]);
        cos_sum = next;
    }
    int total = behavior_function_builder_slot(builder, PIN_F64);
    emit(BF_ADD, PIN_F64, total, sum, cos_sum);
    int state = behavior_function_builder_slot(builder, PIN_F64);
    emit(BF_VARIABLE, PIN_F64, state, 0, 0);
    int final = behavior_function_builder_slot(builder, PIN_F64);
    emit(BF_ADD, PIN_F64, final, total, state);
    emit(BF_RESULT, PIN_F64, 0, final, 0);
    emit(BF_RESULT, PIN_F32, 1, argument[6], 0);
    emit(BF_RESULT, PIN_BOOL, 2, argument[7], 0);
    emit(BF_RETURN, PIN_I32, 0, 0, 0);
    emit(BF_END, PIN_I32, 0, 0, 0);
    REQUIRE(finish());
    const double cases[] = { NAN, INFINITY, -INFINITY, -0., 0., 1.e-40, .375 };
    for (int sample = 0; sample < 7; sample++) {
        for (int i = 0; i < 6; i++) {
            args[i].d = i ? .1 + i * .2 : cases[sample];
        }
        args[6].f = (float)cases[sample];
        args[7].b = sample % 2;
        double a = cos_sin_sine(args[0].d), b = cos_sin_cosine(a);
        for (int i = 1; i < 6; i++) {
            double x = cos_sin_sine(args[i].d);
            a += x;
            b += cos_sin_cosine(x);
        }
        double expected = (a + b) + 2.75;
        for (int native = 0; native < 2; native++) {
            REQUIRE(run(args, 8, &bias, results, native));
            REQUIRE(same_real(results[0].d, expected));
            REQUIRE(same_real(results[1].f, args[6].f));
            REQUIRE(results[2].b == args[7].b && bias.d == 2.75);
        }
    }
    return true;
}
void test_compiled_math()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(public_math_edge_values_match_native_semantics);
    RUN_TEST_CASE(public_math_calls_preserve_live_registers_and_state);
    pu_clear_setup();
}
