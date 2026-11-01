#include "execution/function_builder.h"
#include "execution/status.h"
#include "support/cos_sin.h"
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
static void emit(behavior_function_op op, pin_type type, int output, int left, int right)
{
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = op,
                                         .type = type,
                                         .output = output,
                                         .left = left,
                                         .right = right,
                                         .target = op == BF_DIV || op == BF_MOD ? -1
                                                 : op == BF_RESULT ? output
                                                                   : 0,
                                         .value = { .type = type } }
    );
}
static bool finish()
{
    char error[256];
    function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        fprintf(stderr, "public integer: %s\n", error);
    }
    REQUIRE(function && behavior_function_compile(function));
    return true;
}
static int run(const pin_value *args, int count, pin_value *result, bool native)
{
    arena_free(&storage);
    behavior_function_context context = { .arguments = args,
                                          .argument_count = count,
                                          .results = result,
                                          .budget = 1024 };
    behavior_function_bind_storage(&context, &storage);
    if (!behavior_function_bind(function, &context, NULL, 0)) {
        return FLOW_ERR_ARGUMENT;
    }
    return behavior_function_run(function, &context, native);
}
static pin_value integer(pin_type type, uint64_t bits)
{
    pin_value v = { .type = type };
    if (type == PIN_I32) {
        uint32_t low = (uint32_t)bits;
        memcpy(&v.i, &low, sizeof low);
    } else {
        memcpy(&v.i64, &bits, sizeof bits);
    }
    return v;
}
static int64_t signed_value(pin_value v)
{
    return v.type == PIN_I32 ? v.i : v.i64;
}
static bool same(pin_value a, pin_value b)
{
    REQUIRE(a.type == b.type);
    switch (a.type) {
        case PIN_BOOL: REQUIRE(a.b == b.b); break;
        case PIN_I8: REQUIRE(a.i8 == b.i8); break;
        case PIN_U8: REQUIRE(a.u8 == b.u8); break;
        case PIN_I16: REQUIRE(a.i16 == b.i16); break;
        case PIN_U16: REQUIRE(a.u16 == b.u16); break;
        case PIN_I32: REQUIRE(a.i == b.i); break;
        case PIN_U32: REQUIRE(a.u == b.u); break;
        case PIN_I64: REQUIRE(a.i64 == b.i64); break;
        case PIN_U64: REQUIRE(a.u64 == b.u64); break;
        case PIN_F64: REQUIRE(a.d == b.d); break;
        default: REQUIRE(false);
    }
    return true;
}
/* Unsigned arithmetic defines wrapping independently of signed overflow. */
static pin_value reference(behavior_function_op op, pin_type type, const pin_value *args)
{
    int64_t x = signed_value(args[0]), y = signed_value(args[1]);
    uint64_t bits = 0;
    switch (op) {
        case BF_ADD: bits = (uint64_t)x + (uint64_t)y; break;
        case BF_SUB: bits = (uint64_t)x - (uint64_t)y; break;
        case BF_MUL: bits = (uint64_t)x * (uint64_t)y; break;
        /* x / 0 is 0 with remainder x; x / -1 negates, so the minimum wraps to
         * itself, with remainder 0. */
        case BF_DIV:
            bits = !y ? 0 : y == -1 ? 0 - (uint64_t)x : (uint64_t)(x / y);
            break;
        case BF_MOD:
            bits = !y ? (uint64_t)x : y == -1 ? 0 : (uint64_t)(x % y);
            break;
        case BF_NEG: bits = 0 - (uint64_t)x; break;
        case BF_ABS: bits = x < 0 ? 0 - (uint64_t)x : (uint64_t)x; break;
        case BF_MIN: bits = (uint64_t)(x < y ? x : y); break;
        case BF_MAX: bits = (uint64_t)(x > y ? x : y); break;
        case BF_SELECT: { /* Clamp's native contract is min(value, high), max(low). */
            int64_t high = signed_value(args[2]);
            int64_t bounded = x < high ? x : high;
            bits = (uint64_t)(bounded > y ? bounded : y);
            break;
        }
        case BF_LT: return (pin_value){ .type = PIN_BOOL, .b = x < y };
        case BF_LE: return (pin_value){ .type = PIN_BOOL, .b = x <= y };
        case BF_GT: return (pin_value){ .type = PIN_BOOL, .b = x > y };
        case BF_GE: return (pin_value){ .type = PIN_BOOL, .b = x >= y };
        case BF_EQ: return (pin_value){ .type = PIN_BOOL, .b = x == y };
        case BF_NE: return (pin_value){ .type = PIN_BOOL, .b = x != y };
        default: break;
    }
    return integer(type, bits);
}
static bool primitive(behavior_function_op op, pin_type type, int count, bool compare)
{
    builder = behavior_function_builder_new();
    pin_value args[3] = { { .type = type }, { .type = type }, { .type = type } };
    pin_value result = { .type = compare ? PIN_BOOL : type };
    behavior_function_builder_signature(builder, args, count, &result, 1, NULL, 0);
    int inputs[3];
    for (int i = 0; i < count; i++) {
        inputs[i] = behavior_function_builder_slot(builder, type);
        emit(BF_ARGUMENT, type, inputs[i], i, 0);
    }
    int output = behavior_function_builder_slot(builder, result.type);
    if (count == 3) {
        int bounded = behavior_function_builder_slot(builder, type);
        emit(BF_MIN, type, bounded, inputs[0], inputs[2]);
        emit(BF_MAX, type, output, bounded, inputs[1]);
    } else {
        emit(op, type, output, inputs[0], count == 2 ? inputs[1] : 0);
    }
    emit(BF_RESULT, result.type, 0, output, 0);
    emit(BF_RETURN, PIN_I32, 0, 0, 0);
    emit(BF_END, PIN_I32, 0, 0, 0);
    REQUIRE(finish());
    int64_t lo = type == PIN_I32 ? INT32_MIN : INT64_MIN;
    int64_t hi = type == PIN_I32 ? INT32_MAX : INT64_MAX;
    const int64_t values[] = { lo,         lo + 1,     -17,        -1,
                               0,          1,          3,          17,
                               hi - 1,     hi,         255,        256,
                               0x40000000, 0x7fffffff, 0x80000000, 0xffffffff };
    int samples = count == 3 ? 4096 : count == 2 ? 256 : 16;
    for (int k = 0; k < samples; k++) {
        int index = k;
        for (int i = 0; i < count; i++) {
            args[i] = integer(type, (uint64_t)values[index % 16]);
            index /= 16;
        }
        pin_value expected = reference(op, type, args);
        for (int native = 0; native < 2; native++) {
            pin_value out = result;
            REQUIRE(run(args, count, &out, native) == FLOW_OK);
            REQUIRE(same(out, expected));
        }
    }
    cleanup();
    return true;
}
TEST_CASE(public_integer_wrapping_division_and_comparisons)
{
    const behavior_function_op ops[] = { BF_ADD, BF_SUB,   BF_MUL, BF_DIV,
                                         BF_MOD, BF_NEG,   BF_ABS, BF_MIN,
                                         BF_MAX, BF_SELECT };
    for (int width = 0; width < 2; width++) {
        for (int i = 0; i < 10; i++) {
            REQUIRE(primitive(
                ops[i],
                width ? PIN_I64 : PIN_I32,
                i == 9             ? 3
                : i == 5 || i == 6 ? 1
                                   : 2,
                false
            ));
        }
    }
    const behavior_function_op comparisons[] = { BF_LT, BF_LE, BF_GT,
                                                 BF_GE, BF_EQ, BF_NE };
    for (int i = 0; i < 6; i++) {
        REQUIRE(primitive(comparisons[i], PIN_I32, 2, true));
    }
    return true;
}
static bool conversion(pin_type from, pin_type to)
{
    builder = behavior_function_builder_new();
    pin_value input = { .type = from }, output = { .type = to };
    behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
    int a = behavior_function_builder_slot(builder, from),
        b = behavior_function_builder_slot(builder, to);
    emit(BF_ARGUMENT, from, a, 0, 0);
    if (from == to) {
        b = a;
    } else {
        emit(BF_CAST, to, b, a, 0);
    }
    emit(BF_RESULT, to, 0, b, 0);
    emit(BF_RETURN, PIN_I32, 0, 0, 0);
    emit(BF_END, PIN_I32, 0, 0, 0);
    REQUIRE(finish());
    const double reals[] = { NAN,
                             INFINITY,
                             -INFINITY,
                             -0.,
                             .99,
                             -1.99,
                             254.99,
                             255.,
                             256.,
                             -0x1p31,
                             0x1p31 - 1.,
                             0x1p31,
                             -0x1p63,
                             nextafter(-0x1p63, 0),
                             nextafter(0x1p63, 0),
                             0x1p63 };
    const int64_t integers[] = {
        INT64_MIN,  INT64_MIN + 1, -17,       -1,  0,   1,          3,
        17,         INT64_MAX - 1, INT64_MAX, 255, 256, 0x40000000, 0x7fffffff,
        0x80000000, 0xffffffff
    };
    for (int i = 0; i < 16; i++) {
        input = (pin_value){ .type = from };
        switch (from) {
            case PIN_F32: input.f = (float)reals[i]; break;
            case PIN_F64: input.d = reals[i]; break;
            case PIN_I8: input.i8 = (int8_t)integers[i]; break;
            case PIN_U8: input.u8 = (uint8_t)integers[i]; break;
            case PIN_I16: input.i16 = (int16_t)integers[i]; break;
            case PIN_U16: input.u16 = (uint16_t)integers[i]; break;
            case PIN_I32: input.i = (int32_t)integers[i]; break;
            case PIN_U32: input.u = (uint32_t)integers[i]; break;
            case PIN_I64: input.i64 = integers[i]; break;
            case PIN_U64: input.u64 = (uint64_t)integers[i]; break;
            default: REQUIRE(false);
        }
        pin_value expected = input;
        if (to == PIN_F64) {
            expected = (pin_value){ .type = to,
                                    .d = from == PIN_U8  ? input.u8
                                       : from == PIN_I32 ? input.i
                                                         : (double)input.i64 };
        } else if (from != to) {
            double x = from == PIN_F32 ? input.f
                     : from == PIN_F64 ? input.d
                                       : input.i;
            int64_t lo = to == PIN_U8  ? 0
                       : to == PIN_I32 ? INT32_MIN
                                       : INT64_MIN;
            int64_t hi = to == PIN_U8  ? UINT8_MAX
                       : to == PIN_I32 ? INT32_MAX
                                       : INT64_MAX;
            int64_t value = isnan(x)        ? 0
                          : x <= (double)lo ? lo
                          : x >= (double)hi ? hi
                                            : (int64_t)x;
            expected = (pin_value){ .type = to };
            if (to == PIN_U8) {
                expected.u8 = (uint8_t)value;
            }
            if (to == PIN_I32) {
                expected.i = (int32_t)value;
            }
            if (to == PIN_I64) {
                expected.i64 = from == PIN_I32 ? input.i : value;
            }
        }
        for (int native = 0; native < 2; native++) {
            output = (pin_value){ .type = to };
            REQUIRE(run(&input, 1, &output, native) == FLOW_OK);
            REQUIRE(same(output, expected));
        }
    }
    cleanup();
    return true;
}
TEST_CASE(public_integer_flags_and_live_math_registers)
{
    builder = behavior_function_builder_new();
    pin_value args[4] = { { .type = PIN_I64 },
                          { .type = PIN_I64 },
                          { .type = PIN_U32 },
                          { .type = PIN_U32 } };
    pin_value outputs[2] = { { .type = PIN_I64 }, { .type = PIN_BOOL } };
    behavior_function_builder_signature(builder, args, 4, outputs, 2, NULL, 0);
    int inputs[4];
    for (int i = 0; i < 4; i++) {
        inputs[i] = behavior_function_builder_slot(builder, args[i].type);
        emit(BF_ARGUMENT, args[i].type, inputs[i], i, 0);
    }
    int sum = behavior_function_builder_slot(builder, PIN_I64);
    int real = behavior_function_builder_slot(builder, PIN_F64);
    int sine = behavior_function_builder_slot(builder, PIN_F64);
    int converted = behavior_function_builder_slot(builder, PIN_I64);
    int total = behavior_function_builder_slot(builder, PIN_I64);
    int flags = behavior_function_builder_slot(builder, PIN_BOOL);
    emit(BF_ADD, PIN_I64, sum, inputs[0], inputs[1]);
    emit(BF_CAST, PIN_F64, real, inputs[0], 0);
    emit(BF_SIN, PIN_F64, sine, real, 0);
    emit(BF_CAST, PIN_I64, converted, sine, 0);
    emit(BF_ADD, PIN_I64, total, sum, converted);
    emit(BF_FLAGS, PIN_BOOL, flags, inputs[2], inputs[3]);
    emit(BF_RESULT, PIN_I64, 0, total, 0);
    int result = behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_RESULT, .type = PIN_BOOL, .left = flags, .target = 1 }
    );
    (void)result;
    emit(BF_RETURN, PIN_I32, 0, 0, 0);
    emit(BF_END, PIN_I32, 0, 0, 0);
    REQUIRE(finish());
    const int64_t values[] = {
        INT64_MIN,  INT64_MIN + 1, -17,       -1,  0,   1,          3,
        17,         INT64_MAX - 1, INT64_MAX, 255, 256, 0x40000000, 0x7fffffff,
        0x80000000, 0xffffffff
    };
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            args[0].i64 = values[x];
            args[1].i64 = values[y];
            args[2].u = (uint32_t)values[x];
            args[3].u = (uint32_t)values[y];
            double wave = cos_sin_sine((double)values[x]);
            int64_t extra = isnan(wave) ? 0 : (int64_t)wave;
            pin_value expected = integer(
                PIN_I64,
                (uint64_t)values[x] + (uint64_t)values[y] + (uint64_t)extra
            );
            for (int native = 0; native < 2; native++) {
                pin_value out[2] = { { .type = PIN_I64 }, { .type = PIN_BOOL } };
                REQUIRE(run(args, 4, out, native) == FLOW_OK);
                REQUIRE(same(out[0], expected));
                REQUIRE(out[1].b == ((args[2].u & args[3].u) != 0));
            }
        }
    }
    return true;
}
TEST_CASE(public_integer_loop_budget_preserves_completed_writes)
{
    builder = behavior_function_builder_new();
    pin_value initial = { .type = PIN_I32 };
    behavior_function_builder_signature(builder, NULL, 0, NULL, 0, &initial, 1);
    int count = behavior_function_builder_slot(builder, PIN_I32);
    int one = behavior_function_builder_slot(builder, PIN_I32);
    int limit = behavior_function_builder_slot(builder, PIN_I32);
    int condition = behavior_function_builder_slot(builder, PIN_BOOL);
    emit(BF_VARIABLE, PIN_I32, count, 0, 0);
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .output = one,
                                         .value = { .type = PIN_I32, .i = 1 } }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .output = limit,
                                         .value = { .type = PIN_I32, .i = 4 } }
    );
    int loop = behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_CHECK });
    emit(BF_ADD, PIN_I32, count, count, one);
    emit(BF_STORE, PIN_I32, 0, count, 0);
    emit(BF_LT, PIN_I32, condition, count, limit);
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_BRANCH, .left = condition, .target = loop }
    );
    emit(BF_END, PIN_I32, 0, 0, 0);
    REQUIRE(finish());
    for (int budget = 1; budget <= 12; budget++) {
        for (int native = 0; native < 2; native++) {
            pin_value value = initial;
            arena_free(&storage);
            behavior_function_context context = { .variables = &value,
                                                  .budget = (uintptr_t)budget };
            behavior_function_bind_storage(&context, &storage);
            REQUIRE(behavior_function_bind(function, &context, NULL, 0));
            int status = behavior_function_run(function, &context, native);
            REQUIRE(status == (budget < 4 ? FLOW_ERR_STEP_BUDGET : FLOW_OK));
            REQUIRE(value.i == (budget < 4 ? budget : 4));
        }
    }
    return true;
}
void test_compiled_integers()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(public_integer_wrapping_division_and_comparisons);
    RUN_TEST_CASE(public_integer_flags_and_live_math_registers);
    RUN_TEST_CASE(public_integer_loop_budget_preserves_completed_writes);
    pu_clear_setup();
}
