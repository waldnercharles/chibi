#include "execution/function_builder.h"
#include "execution/status.h"
#include "third_party/pico_headers/pico_unit.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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
static int slot(pin_type type)
{
    return behavior_function_builder_slot(builder, type);
}
static void emit(behavior_function_instruction instruction)
{
    behavior_function_builder_emit(builder, instruction);
}
static bool finish()
{
    char error[256];
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "public conversion: %s\n", error);
    }
    REQUIRE(function && behavior_function_compile(function));
    return true;
}
static bool run(const pin_value *arguments, int count, pin_value *output, bool native)
{
    arena_free(&storage);
    behavior_function_context context = { .arguments = arguments,
                                          .argument_count = count,
                                          .results = output,
                                          .budget = 128 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    REQUIRE(behavior_function_run(function, &context, native) == FLOW_OK);
    return true;
}
static pin_value number(pin_type type, int value)
{
    pin_value out = { .type = type };
    switch (type) {
        case PIN_BOOL: out.b = value & 1; break;
        case PIN_U8: out.u8 = (uint8_t)value; break;
        case PIN_I32: out.i = value; break;
        case PIN_I64: out.i64 = value; break;
        case PIN_F32: out.f = (float)value; break;
        case PIN_F64: out.d = value; break;
        default: break;
    }
    return out;
}
static bool equal(pin_value actual, pin_value expected)
{
    REQUIRE(actual.type == expected.type);
    switch (expected.type) {
        case PIN_BOOL: REQUIRE(actual.b == expected.b); break;
        case PIN_U8: REQUIRE(actual.u8 == expected.u8); break;
        case PIN_I32: REQUIRE(actual.i == expected.i); break;
        case PIN_I64: REQUIRE(actual.i64 == expected.i64); break;
        case PIN_F32:
            REQUIRE(isnan(expected.f) ? isnan(actual.f) : actual.f == expected.f);
            REQUIRE(expected.f != 0 || signbit(expected.f) == signbit(actual.f));
            break;
        case PIN_F64:
            REQUIRE(isnan(expected.d) ? isnan(actual.d) : actual.d == expected.d);
            REQUIRE(expected.d != 0 || signbit(expected.d) == signbit(actual.d));
            break;
        default: REQUIRE(false);
    }
    return true;
}
static bool supported(pin_type from, pin_type to)
{
    if ((from == PIN_BOOL || from == PIN_F32 || from == PIN_F64) &&
        (to == PIN_U8 || to == PIN_I32 || to == PIN_I64)) {
        return true;
    }
    if ((from == PIN_BOOL || from == PIN_U8 || from == PIN_I32 || from == PIN_I64) &&
        (to == PIN_F32 || to == PIN_F64)) {
        return true;
    }
    return from == PIN_I32 && to == PIN_I64;
}
static pin_value expected(pin_value input, pin_type to)
{
    double x = input.type == PIN_BOOL ? input.b
             : input.type == PIN_U8   ? input.u8
             : input.type == PIN_I32  ? input.i
             : input.type == PIN_I64  ? (double)input.i64
             : input.type == PIN_F32  ? input.f
                                      : input.d;
    pin_value out = { .type = to };
    if (to == PIN_F32) {
        out.f = (float)x;
        return out;
    }
    if (to == PIN_F64) {
        out.d = x;
        return out;
    }
    int64_t low = to == PIN_U8 ? 0 : to == PIN_I32 ? INT32_MIN : INT64_MIN;
    int64_t high = to == PIN_U8  ? UINT8_MAX
                 : to == PIN_I32 ? INT32_MAX
                                 : INT64_MAX;
    int64_t value = isnan(x)          ? 0
                  : x <= (double)low  ? low
                  : x >= (double)high ? high
                                      : (int64_t)x;
    if (to == PIN_U8) {
        out.u8 = (uint8_t)value;
    }
    if (to == PIN_I32) {
        out.i = (int32_t)value;
    }
    if (to == PIN_I64) {
        out.i64 = input.type == PIN_I32 ? input.i : value;
    }
    return out;
}
TEST_CASE(public_numeric_conversions_preserve_edges_and_real_widths)
{
    const pin_type types[] = { PIN_BOOL, PIN_U8,  PIN_I32,
                               PIN_I64,  PIN_F32, PIN_F64 };
    const double values[] = { -INFINITY, -0x1p63, -257.5, -1.5,   -0.,      0.,
                              1.5,       255.5,   0x1p31, 0x1p63, INFINITY, NAN };
    for (int from = 0; from < 6; from++) {
        for (int to = 0; to < 6; to++) {
            if (!supported(types[from], types[to])) {
                continue;
            }
            builder = behavior_function_builder_new();
            pin_value argument = { .type = types[from] },
                      result = { .type = types[to] };
            behavior_function_builder_signature(builder, &argument, 1, &result, 1, NULL, 0);
            int input = slot(argument.type), output = slot(result.type);
            emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                                  .type = argument.type,
                                                  .output = input,
                                                  .value = argument });
            if (argument.type == PIN_BOOL) {
                int yes = slot(result.type), no = slot(result.type);
                emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                                      .type = result.type,
                                                      .output = yes,
                                                      .value = number(result.type, 1) });
                emit((behavior_function_instruction){ .op = BF_CONSTANT,
                                                      .type = result.type,
                                                      .output = no,
                                                      .value = number(result.type, 0) });
                emit((behavior_function_instruction){ .op = BF_SELECT,
                                                      .type = result.type,
                                                      .output = output,
                                                      .left = input,
                                                      .right = yes,
                                                      .target = no });
            } else if (result.type == PIN_F32) {
                int wide = slot(PIN_F64);
                emit((
                    behavior_function_instruction
                ){ .op = BF_CAST, .type = PIN_F64, .output = wide, .left = input });
                emit((behavior_function_instruction){ .op = BF_CONVERT,
                                                      .type = PIN_F32,
                                                      .output = output,
                                                      .left = wide });
            } else {
                emit((behavior_function_instruction){ .op = BF_CAST,
                                                      .type = result.type,
                                                      .output = output,
                                                      .left = input });
            }
            emit((behavior_function_instruction){ .op = BF_RESULT,
                                                  .type = result.type,
                                                  .left = output });
            emit((behavior_function_instruction){ .op = BF_END });
            REQUIRE(finish());
            for (int value = 0; value < 12; value++) {
                switch (argument.type) {
                    case PIN_BOOL: argument.b = value & 1; break;
                    case PIN_U8:
                        argument.u8 = value == 11 ? UINT8_MAX : (uint8_t)value;
                        break;
                    case PIN_I32:
                        argument.i = value == 11 ? INT_MAX : INT_MIN + value;
                        break;
                    case PIN_I64:
                        argument.i64 = value == 11 ? INT64_MAX : INT64_MIN + value;
                        break;
                    case PIN_F32: argument.f = (float)values[value]; break;
                    case PIN_F64: argument.d = values[value]; break;
                    default: REQUIRE(false);
                }
                pin_value want = expected(argument, result.type);
                for (int native = 0; native < 2; native++) {
                    REQUIRE(run(&argument, 1, &result, native));
                    REQUIRE(equal(result, want));
                }
            }
            cleanup();
        }
    }
    return true;
}
TEST_CASE(public_select_preserves_each_scalar_width)
{
    const pin_type types[] = { PIN_BOOL, PIN_U8,  PIN_I32,
                               PIN_I64,  PIN_F32, PIN_F64 };
    for (int t = 0; t < 6; t++) {
        builder = behavior_function_builder_new();
        pin_value args[] = { { .type = PIN_BOOL },
                             number(types[t], 3),
                             number(types[t], 6) };
        pin_value output = { .type = types[t] };
        behavior_function_builder_signature(builder, args, 3, &output, 1, NULL, 0);
        int inputs[3];
        for (int i = 0; i < 3; i++) {
            inputs[i] = slot(args[i].type);
            emit((behavior_function_instruction){ .op = BF_ARGUMENT,
                                                  .type = args[i].type,
                                                  .output = inputs[i],
                                                  .left = i,
                                                  .value = args[i] });
        }
        int result = slot(output.type);
        emit((behavior_function_instruction){ .op = BF_SELECT,
                                              .type = output.type,
                                              .output = result,
                                              .left = inputs[0],
                                              .right = inputs[2],
                                              .target = inputs[1] });
        emit((behavior_function_instruction){ .op = BF_RESULT,
                                              .type = output.type,
                                              .left = result });
        emit((behavior_function_instruction){ .op = BF_END });
        REQUIRE(finish());
        for (int pick = 0; pick < 2; pick++) {
            args[0].b = pick;
            for (int native = 0; native < 2; native++) {
                REQUIRE(run(args, 3, &output, native));
                REQUIRE(equal(output, args[pick ? 2 : 1]));
            }
        }
        cleanup();
    }
    return true;
}
void test_compiled_conversions()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(public_numeric_conversions_preserve_edges_and_real_widths);
    pu_clear_setup();
}
void test_compiled_selects()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(public_select_preserves_each_scalar_width);
    pu_clear_setup();
}
