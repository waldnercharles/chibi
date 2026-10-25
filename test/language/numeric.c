#include "language/numeric.h"

#include "third_party/pico_headers/pico_unit.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Draft only: 22 direct-API cases, no parser, builder, backend or host calls.
 * No selector/CMake/policy registration or execution is authorized here.
 * Every normal fixture owner is capped at 128 API/graph visits, 8,388,608
 * charged limb-work units and 32 KiB including its fixed arithmetic scratch.
 * Exactly 30 constructor attempts are scheduled if all cases reach the end;
 * one deliberately fails. At most one owner lives at a time.
 * The only long input is 1,026 bytes (1,027-byte stack buffer). */
static script_numeric *numbers;
static script_numeric_error error;
static const script_numeric_limits fixture_limits = { 128, 8388608, 32768 };
static void cleanup()
{
    script_numeric_free(numbers);
    numbers = NULL;
}
static void setup()
{
    numbers = script_numeric_new(&fixture_limits, &error);
}
static bool restart(script_numeric_limits limits)
{
    cleanup();
    numbers = script_numeric_new(&limits, &error);
    REQUIRE(numbers);
    return true;
}
static const script_number *read_number(const char *text, script_number_kind kind)
{
    bool negative = text[0] == '-';
    if (negative) {
        text++;
    }
    const script_number *v = script_number_read(numbers, text, strlen(text), kind, &error);
    if (v && negative) {
        v = script_number_unary(numbers, SCRIPT_OP_NEG, v, &error);
    }
    return v;
}
static const script_number *integer(const char *text)
{
    return read_number(text, SCRIPT_NUMBER_INTEGER);
}
static const script_number *real(const char *text)
{
    return read_number(text, SCRIPT_NUMBER_REAL);
}
static const script_number *binary(script_operator op, const script_number *a, const script_number *b)
{
    if (!a || !b) {
        return NULL;
    }
    return script_number_binary(numbers, op, a, b, &error);
}
static const script_number *power(unsigned shift)
{
    char text[12];
    snprintf(text, sizeof text, "%u", shift);
    const script_number *one = integer("1"), *count = integer(text);
    return binary(SCRIPT_OP_SHIFT_LEFT, one, count);
}
static bool expect_integer(const script_number *v, pin_type type, uint64_t expected)
{
    REQUIRE(v);
    pin_value value;
    REQUIRE(script_number_materialize(numbers, v, type, &value, &error));
    uint64_t actual;
    switch (type) {
        case PIN_I8: actual = (uint64_t)(int64_t)value.i8; break;
        case PIN_U8: actual = value.u8; break;
        case PIN_I16: actual = (uint64_t)(int64_t)value.i16; break;
        case PIN_U16: actual = value.u16; break;
        case PIN_I32: actual = (uint64_t)(int64_t)value.i; break;
        case PIN_U32: actual = value.u; break;
        case PIN_I64: actual = (uint64_t)value.i64; break;
        case PIN_U64: actual = value.u64; break;
        default: REQUIRE(false); return false;
    }
    REQUIRE(actual == expected);
    return true;
}
static bool expect_float(const script_number *v, pin_type type, uint64_t expected)
{
    REQUIRE(v);
    pin_value value;
    REQUIRE(script_number_materialize(numbers, v, type, &value, &error));
    uint64_t actual;
    if (type == PIN_F32) {
        uint32_t bits;
        memcpy(&bits, &value.f, sizeof bits);
        actual = bits;
    } else {
        memcpy(&actual, &value.d, sizeof actual);
    }
    REQUIRE(actual == expected);
    return true;
}
static bool expect_failure(const script_number *v, pin_type type, script_numeric_code code)
{
    REQUIRE(v);
    pin_value out = { .type = PIN_U64, .u64 = UINT64_C(0x123456789abcdef0) };
    REQUIRE(!script_number_materialize(numbers, v, type, &out, &error));
    REQUIRE(error.code == code);
    REQUIRE(out.type == PIN_U64 && out.u64 == UINT64_C(0x123456789abcdef0));
    return true;
}
TEST_CASE(numeric_kind_and_decimal_scale)
{
    REQUIRE(numbers);
    const script_number *i = integer("100"), *r = real("1e2"), *z = real("0.0"),
                        *h = integer("0x0000ff"), *scaled = real("00010.000e-1");
    REQUIRE(i && r && z && h && scaled);
    REQUIRE(script_number_kind_read(i) == SCRIPT_NUMBER_INTEGER);
    REQUIRE(script_number_kind_read(r) == SCRIPT_NUMBER_REAL);
    REQUIRE(script_number_kind_read(z) == SCRIPT_NUMBER_REAL);
    REQUIRE(expect_integer(i, PIN_I32, 100));
    REQUIRE(expect_integer(r, PIN_I32, 100));
    REQUIRE(expect_integer(h, PIN_U8, 255));
    REQUIRE(expect_integer(scaled, PIN_I32, 1));
    return true;
}
TEST_CASE(numeric_all_integer_storage_boundaries)
{
    static const struct
    {
        pin_type type;
        const char *minimum, *maximum, *outside;
        uint64_t low, high;
    } rows[] = {
        { PIN_I8, "-128", "127", "128", (uint64_t)(int64_t)INT8_MIN, INT8_MAX },
        { PIN_U8, "0", "255", "256", 0, UINT8_MAX },
        { PIN_I16, "-32768", "32767", "32768", (uint64_t)(int64_t)INT16_MIN, INT16_MAX },
        { PIN_U16, "0", "65535", "65536", 0, UINT16_MAX },
        { PIN_I32, "-2147483648", "2147483647", "2147483648", (uint64_t)(int64_t)INT32_MIN, INT32_MAX },
        { PIN_U32, "0", "4294967295", "4294967296", 0, UINT32_MAX },
        { PIN_I64,
          "-9223372036854775808",
          "9223372036854775807",
          "9223372036854775808",
          (uint64_t)INT64_MIN,
          INT64_MAX },
        { PIN_U64, "0", "18446744073709551615", "18446744073709551616", 0, UINT64_MAX },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof *rows; i++) {
        const script_number *lo = integer(rows[i].minimum),
                            *hi = integer(rows[i].maximum),
                            *outside = integer(rows[i].outside);
        REQUIRE(expect_integer(lo, rows[i].type, rows[i].low));
        REQUIRE(expect_integer(hi, rows[i].type, rows[i].high));
        REQUIRE(expect_failure(outside, rows[i].type, SCRIPT_NUMERIC_RANGE));
    }
    const script_number *negative = integer("-1");
    const pin_type unsigned_types[] = { PIN_U8, PIN_U16, PIN_U32, PIN_U64 };
    for (unsigned i = 0; i < 4; i++) {
        REQUIRE(expect_failure(negative, unsigned_types[i], SCRIPT_NUMERIC_RANGE));
    }
    return true;
}
TEST_CASE(numeric_preserves_wide_integer_intermediates)
{
    const script_number *big = integer("9007199254740993"),
                        *maximum = integer("18446744073709551615"),
                        *one = integer("1");
    REQUIRE(expect_failure(big, PIN_F64, SCRIPT_NUMERIC_PRECISION));
    REQUIRE(expect_integer(big, PIN_U64, UINT64_C(9007199254740993)));
    const script_number *above = binary(SCRIPT_OP_ADD, maximum, one);
    const script_number *back = binary(SCRIPT_OP_SUB, above, one);
    REQUIRE(expect_integer(back, PIN_U64, UINT64_MAX));
    REQUIRE(expect_failure(above, PIN_U64, SCRIPT_NUMERIC_RANGE));
    REQUIRE(expect_integer(maximum, PIN_U64, UINT64_MAX));
    return true;
}
TEST_CASE(numeric_real_rounds_once_to_target)
{
    static const struct
    {
        const char *text;
        pin_type type;
        uint64_t bits;
    } rows[] = {
        { "0.1", PIN_F32, UINT64_C(0x3dcccccd) },
        { "0.1", PIN_F64, UINT64_C(0x3fb999999999999a) },
        { "16777217.0", PIN_F32, UINT64_C(0x4b800000) },
        { "16777219.0", PIN_F32, UINT64_C(0x4b800002) },
        { "9007199254740993.0", PIN_F64, UINT64_C(0x4340000000000000) },
        { "9007199254740995.0", PIN_F64, UINT64_C(0x4340000000000002) },
        /* Just above the F32 midpoint; rounding via F64 would incorrectly give 1. */
        { "1.0000000596046447753906250000000000000001", PIN_F32, UINT64_C(0x3f800001) },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof *rows; i++) {
        REQUIRE(expect_float(real(rows[i].text), rows[i].type, rows[i].bits));
    }
    REQUIRE(expect_failure(integer("16777217"), PIN_F32, SCRIPT_NUMERIC_PRECISION));
    return true;
}
TEST_CASE(numeric_contextual_integral_real_is_checked)
{
    const script_number *integral = real("1e2"), *fraction = real("1.5"),
                        *zero = real("-0.0");
    REQUIRE(expect_integer(integral, PIN_I32, 100));
    REQUIRE(expect_failure(fraction, PIN_I32, SCRIPT_NUMERIC_FRACTION));
    REQUIRE(expect_integer(zero, PIN_I8, 0));
    return true;
}
TEST_CASE(numeric_division_depends_on_operand_kind)
{
    const script_number *one = integer("1"), *real_one = real("1.0"),
                        *sixty = integer("60");
    const script_number *whole = binary(SCRIPT_OP_DIV, one, sixty),
                        *fraction = binary(SCRIPT_OP_DIV, real_one, sixty);
    REQUIRE(expect_integer(whole, PIN_I32, 0));
    REQUIRE(expect_float(whole, PIN_F32, 0));
    REQUIRE(expect_float(fraction, PIN_F64, UINT64_C(0x3f91111111111111)));
    const script_number *zero = integer("0");
    REQUIRE(!binary(SCRIPT_OP_DIV, real_one, zero));
    REQUIRE(error.code == SCRIPT_NUMERIC_ZERO_DIVISOR);
    REQUIRE(expect_integer(one, PIN_I32, 1));
    return true;
}
TEST_CASE(numeric_integer_division_and_remainder_signs)
{
    static const struct
    {
        const char *left, *right;
        int q, r;
    } rows[] = {
        { "-7", "3", -2, -1 },
        { "7", "-3", -2, 1 },
        { "-7", "-3", 2, -1 },
    };
    for (unsigned i = 0; i < 3; i++) {
        const script_number *a = integer(rows[i].left), *b = integer(rows[i].right);
        REQUIRE(expect_integer(
            binary(SCRIPT_OP_DIV, a, b),
            PIN_I32,
            (uint64_t)(int64_t)rows[i].q
        ));
        REQUIRE(expect_integer(
            binary(SCRIPT_OP_MOD, a, b),
            PIN_I32,
            (uint64_t)(int64_t)rows[i].r
        ));
    }
    const script_number *minimum = integer("-9223372036854775808"),
                        *negative_one = integer("-1");
    REQUIRE(
        expect_integer(binary(SCRIPT_OP_DIV, minimum, negative_one), PIN_U64, UINT64_C(1) << 63)
    );
    return true;
}
TEST_CASE(numeric_exact_rationals_cancel)
{
    const script_number *a = real("0.1"), *b = real("0.2"), *c = real("0.3"),
                        *six = real("0.06"), *half = real("1.5");
    const script_number *sum = binary(SCRIPT_OP_ADD, a, b),
                        *product = binary(SCRIPT_OP_MUL, c, b),
                        *ratio = binary(SCRIPT_OP_DIV, c, b);
    int order = 99;
    REQUIRE(sum && product && ratio);
    REQUIRE(script_number_compare(numbers, sum, c, &order, &error) && !order);
    REQUIRE(script_number_compare(numbers, product, six, &order, &error) && !order);
    REQUIRE(script_number_compare(numbers, ratio, half, &order, &error) && !order);
    REQUIRE(expect_float(binary(SCRIPT_OP_SUB, sum, c), PIN_F64, 0));
    const script_number *negative_a = script_number_unary(numbers, SCRIPT_OP_NEG, a, &error),
                        *negative_b = script_number_unary(numbers, SCRIPT_OP_NEG, b, &error);
    REQUIRE(negative_a && negative_b);
    REQUIRE(script_number_compare(numbers, a, b, &order, &error) && order < 0);
    REQUIRE(script_number_compare(numbers, b, a, &order, &error) && order > 0);
    REQUIRE(script_number_compare(numbers, negative_a, negative_b, &order, &error) && order > 0);
    REQUIRE(script_number_compare(numbers, negative_b, negative_a, &order, &error) && order < 0);
    REQUIRE(script_number_compare(numbers, negative_a, b, &order, &error) && order < 0);
    REQUIRE(script_number_compare(numbers, b, negative_a, &order, &error) && order > 0);
    return true;
}
TEST_CASE(numeric_real_zero_sign_is_a_value_fact)
{
    const script_number *positive = real("0.0"), *negative = real("-0.0"),
                        *two = integer("2"), *minus_two = integer("-2");
    REQUIRE(expect_float(negative, PIN_F32, UINT64_C(0x80000000)));
    REQUIRE(expect_float(negative, PIN_F64, UINT64_C(0x8000000000000000)));
    REQUIRE(
        expect_float(script_number_unary(numbers, SCRIPT_OP_NEG, negative, &error), PIN_F64, 0)
    );
    REQUIRE(expect_float(binary(SCRIPT_OP_MUL, negative, two), PIN_F64, UINT64_C(0x8000000000000000)));
    REQUIRE(expect_float(binary(SCRIPT_OP_DIV, negative, minus_two), PIN_F64, 0));
    REQUIRE(
        expect_float(binary(SCRIPT_OP_ADD, negative, negative), PIN_F32, UINT64_C(0x80000000))
    );
    REQUIRE(expect_float(binary(SCRIPT_OP_ADD, positive, negative), PIN_F32, 0));
    REQUIRE(expect_float(binary(SCRIPT_OP_SUB, negative, negative), PIN_F32, 0));
    int order = 99;
    REQUIRE(script_number_compare(numbers, positive, negative, &order, &error) && !order);
    return true;
}
TEST_CASE(numeric_subnormal_and_halfway_rounding)
{
    const script_number *one = real("1.0"), *three = real("3.0");
    const script_number *d149 = power(149), *d150 = power(150),
                        *d1074 = power(1074), *d1075 = power(1075);
    REQUIRE(expect_float(binary(SCRIPT_OP_DIV, one, d149), PIN_F32, 1));
    const script_number *half32 = binary(SCRIPT_OP_DIV, one, d150);
    REQUIRE(expect_float(half32, PIN_F32, 0));
    REQUIRE(
        expect_float(script_number_unary(numbers, SCRIPT_OP_NEG, half32, &error), PIN_F32, UINT64_C(0x80000000))
    );
    REQUIRE(expect_float(binary(SCRIPT_OP_DIV, three, d150), PIN_F32, 2));
    REQUIRE(expect_float(binary(SCRIPT_OP_DIV, one, d1074), PIN_F64, 1));
    REQUIRE(expect_float(binary(SCRIPT_OP_DIV, one, d1075), PIN_F64, 0));
    REQUIRE(expect_float(binary(SCRIPT_OP_DIV, three, d1075), PIN_F64, 2));
    return true;
}
TEST_CASE(numeric_subnormal_carry_reaches_normal)
{
    const script_number *a = real("16777215.0"), *b = real("9007199254740991.0"),
                        *d150 = power(150), *d1075 = power(1075);
    REQUIRE(expect_float(binary(SCRIPT_OP_DIV, a, d150), PIN_F32, UINT64_C(0x00800000)));
    REQUIRE(expect_float(binary(SCRIPT_OP_DIV, b, d1075), PIN_F64, UINT64_C(0x0010000000000000)));
    return true;
}
TEST_CASE(numeric_finite_overflow_and_signed_underflow)
{
    const script_number *coefficient = real("16777215.0"), *scale = power(104);
    const script_number *maximum = binary(SCRIPT_OP_MUL, coefficient, scale),
                        *quarter = power(102), *half = power(103);
    REQUIRE(expect_float(maximum, PIN_F32, UINT64_C(0x7f7fffff)));
    REQUIRE(expect_float(binary(SCRIPT_OP_ADD, maximum, quarter), PIN_F32, UINT64_C(0x7f7fffff)));
    REQUIRE(expect_failure(binary(SCRIPT_OP_ADD, maximum, half), PIN_F32, SCRIPT_NUMERIC_RANGE));
    REQUIRE(expect_failure(real("1e400"), PIN_F64, SCRIPT_NUMERIC_RANGE));
    REQUIRE(expect_float(real("-1e-1000"), PIN_F64, UINT64_C(0x8000000000000000)));
    return true;
}
TEST_CASE(numeric_exact_bitwise_and_shifts)
{
    const script_number *one = integer("1"), *forty = integer("40"),
                        *minus_one = integer("-1"), *byte = integer("255"),
                        *minus_three = integer("-3"), *hundred = integer("100");
    const script_number *wide = binary(SCRIPT_OP_SHIFT_LEFT, one, forty);
    REQUIRE(expect_integer(wide, PIN_I64, UINT64_C(1) << 40));
    REQUIRE(expect_integer(binary(SCRIPT_OP_SHIFT_RIGHT, wide, forty), PIN_I32, 1));
    REQUIRE(
        expect_integer(binary(SCRIPT_OP_SHIFT_RIGHT, minus_three, one), PIN_I32, (uint64_t)(int64_t)-2)
    );
    REQUIRE(expect_integer(binary(SCRIPT_OP_SHIFT_RIGHT, minus_one, hundred), PIN_I32, UINT64_MAX));
    REQUIRE(expect_integer(binary(SCRIPT_OP_BIT_AND, minus_one, byte), PIN_I32, 255));
    REQUIRE(
        expect_integer(binary(SCRIPT_OP_BIT_XOR, minus_one, byte), PIN_I32, (uint64_t)(int64_t)-256)
    );
    REQUIRE(expect_integer(
        script_number_unary(numbers, SCRIPT_OP_BIT_NOT, byte, &error),
        PIN_I32,
        (uint64_t)(int64_t)-256
    ));
    REQUIRE(
        expect_integer(script_number_unary(numbers, SCRIPT_OP_BIT_NOT, minus_one, &error), PIN_I32, 0)
    );
    const script_number *minus_eight = integer("-8"), *three = integer("3"),
                        *zero = integer("0");
    REQUIRE(
        expect_integer(binary(SCRIPT_OP_BIT_OR, minus_eight, three), PIN_I32, (uint64_t)(int64_t)-5)
    );
    REQUIRE(
        expect_integer(script_number_unary(numbers, SCRIPT_OP_BIT_NOT, zero, &error), PIN_I32, UINT64_MAX)
    );
    REQUIRE(!binary(SCRIPT_OP_SHIFT_LEFT, one, minus_one));
    REQUIRE(error.code == SCRIPT_NUMERIC_NEGATIVE_SHIFT);
    REQUIRE(expect_integer(one, PIN_I32, 1));
    return true;
}
TEST_CASE(numeric_shift_count_has_a_finite_limit)
{
    const script_number *zero = integer("0"), *count = integer("4097");
    unsigned before = script_numeric_usage_read(numbers).published_values;
    REQUIRE(!binary(SCRIPT_OP_SHIFT_RIGHT, zero, count));
    REQUIRE(error.code == SCRIPT_NUMERIC_SHIFT_LIMIT);
    REQUIRE(script_numeric_usage_read(numbers).published_values == before);
    REQUIRE(!script_numeric_visit(numbers, 0, &error));
    REQUIRE(error.code == SCRIPT_NUMERIC_SHIFT_LIMIT);
    return true;
}
TEST_CASE(numeric_bit_expansion_stops_before_capacity)
{
    char text[1027];
    text[0] = '0';
    text[1] = 'x';
    text[2] = '8';
    memset(text + 3, '0', 1023);
    text[1026] = 0;
    const script_number *maximum_width = integer(text), *one = integer("1");
    REQUIRE(maximum_width && one);
    const script_number *same = binary(SCRIPT_OP_MUL, maximum_width, one);
    REQUIRE(same);
    REQUIRE(expect_integer(binary(SCRIPT_OP_SUB, same, maximum_width), PIN_I32, 0));
    const script_number *within = binary(SCRIPT_OP_SHIFT_RIGHT, maximum_width, one),
                        *zero = integer("0"), *minus_one = integer("-1");
    REQUIRE(within && zero && minus_one);
    /* A 4,095-bit magnitude leaves the sign bit inside the 4,096-bit cap. */
    const script_number *masked = binary(SCRIPT_OP_BIT_AND, within, minus_one),
                        *joined = binary(SCRIPT_OP_BIT_OR, within, zero);
    REQUIRE(expect_integer(binary(SCRIPT_OP_SUB, masked, within), PIN_I32, 0));
    REQUIRE(expect_integer(binary(SCRIPT_OP_SUB, joined, within), PIN_I32, 0));
    REQUIRE(expect_integer(binary(SCRIPT_OP_BIT_XOR, within, within), PIN_I32, 0));
    unsigned before = script_numeric_usage_read(numbers).published_values;
    REQUIRE(!binary(SCRIPT_OP_SHIFT_LEFT, maximum_width, one));
    REQUIRE(error.code == SCRIPT_NUMERIC_BITS && error.needed == 4097);
    REQUIRE(script_numeric_usage_read(numbers).published_values == before);
    REQUIRE(restart(fixture_limits));
    maximum_width = integer(text);
    zero = integer("0");
    REQUIRE(maximum_width && zero);
    before = script_numeric_usage_read(numbers).published_values;
    /* Result would be zero, but the signed temporary itself needs 4,097 bits. */
    REQUIRE(!binary(SCRIPT_OP_BIT_AND, maximum_width, zero));
    REQUIRE(error.code == SCRIPT_NUMERIC_BITS && error.needed == 4097 && error.limit == 4096);
    REQUIRE(script_numeric_usage_read(numbers).published_values == before);
    REQUIRE(!script_numeric_visit(numbers, 0, &error));
    REQUIRE(error.code == SCRIPT_NUMERIC_BITS && error.needed == 4097);
    return true;
}
TEST_CASE(numeric_no_unsupported_operators_are_inferred)
{
    const script_number *r = real("2.0"), *one = integer("1");
    REQUIRE(r && one);
    REQUIRE(!script_number_unary(numbers, SCRIPT_OP_BIT_NOT, r, &error));
    REQUIRE(error.code == SCRIPT_NUMERIC_KIND);
    REQUIRE(!binary(SCRIPT_OP_SHIFT_LEFT, r, one));
    REQUIRE(error.code == SCRIPT_NUMERIC_KIND);
    REQUIRE(!binary(SCRIPT_OP_MOD, r, one));
    REQUIRE(error.code == SCRIPT_NUMERIC_OPERATOR);
    REQUIRE(!binary(SCRIPT_OP_NOT, one, one));
    REQUIRE(error.code == SCRIPT_NUMERIC_OPERATOR);
    REQUIRE(expect_integer(one, PIN_I32, 1));
    return true;
}
TEST_CASE(numeric_materialization_never_mutates_shared_facts)
{
    const script_number *limit = integer("255"), *negative = integer("-1");
    REQUIRE(limit && negative);
    unsigned before = script_numeric_usage_read(numbers).published_values;
    REQUIRE(expect_failure(limit, PIN_I8, SCRIPT_NUMERIC_RANGE));
    REQUIRE(expect_integer(limit, PIN_U8, 255));
    REQUIRE(expect_integer(limit, PIN_I32, 255));
    REQUIRE(expect_float(limit, PIN_F32, UINT64_C(0x437f0000)));
    REQUIRE(expect_integer(limit, PIN_U8, 255));
    REQUIRE(expect_failure(limit, PIN_I8, SCRIPT_NUMERIC_RANGE));
    REQUIRE(expect_failure(negative, PIN_U64, SCRIPT_NUMERIC_RANGE));
    REQUIRE(expect_integer(negative, PIN_I64, UINT64_MAX));
    REQUIRE(script_numeric_usage_read(numbers).published_values == before);
    return true;
}
TEST_CASE(numeric_step_and_depth_budgets_are_shared)
{
    script_numeric_limits limited = fixture_limits;
    limited.steps = 1;
    REQUIRE(restart(limited));
    const script_number *one = integer("1");
    REQUIRE(one);
    REQUIRE(expect_failure(one, PIN_I32, SCRIPT_NUMERIC_STEPS));
    REQUIRE(script_numeric_usage_read(numbers).steps_remaining == 0);
    REQUIRE(restart(fixture_limits));
    REQUIRE(!script_numeric_visit(numbers, 256, &error));
    REQUIRE(error.code == SCRIPT_NUMERIC_DEPTH);
    REQUIRE(!script_numeric_visit(numbers, 0, &error));
    REQUIRE(error.code == SCRIPT_NUMERIC_DEPTH);
    return true;
}
TEST_CASE(numeric_work_is_reserved_before_input_and_arithmetic)
{
    REQUIRE(numbers);
    uint64_t initial_work = script_numeric_usage_read(numbers).work_remaining;
    uint64_t construction = fixture_limits.work - initial_work;
    REQUIRE(integer("1") && integer("2"));
    uint64_t two_reads = initial_work - script_numeric_usage_read(numbers).work_remaining;
    script_numeric_limits limited = fixture_limits;
    limited.work = construction + 64 + 1;
    REQUIRE(restart(limited));
    REQUIRE(!integer("12"));
    REQUIRE(error.code == SCRIPT_NUMERIC_WORK);
    REQUIRE(script_numeric_usage_read(numbers).published_values == 0);
    REQUIRE(!script_numeric_visit(numbers, 0, &error));
    REQUIRE(error.code == SCRIPT_NUMERIC_WORK);
    /* Reproduce the measured parse cost, leaving exactly 100 work units
     * after the multiplication visit. Loads/cancellation fit; expansion does not. */
    limited.work = construction + two_reads + 64 + 100;
    REQUIRE(restart(limited));
    const script_number *one = integer("1"), *two = integer("2");
    REQUIRE(one && two);
    REQUIRE(script_numeric_usage_read(numbers).published_values == 2);
    REQUIRE(!binary(SCRIPT_OP_MUL, one, two));
    REQUIRE(error.code == SCRIPT_NUMERIC_WORK);
    REQUIRE(script_numeric_usage_read(numbers).published_values == 2);
    return true;
}
TEST_CASE(numeric_storage_includes_scratch_and_arena_capacity)
{
    REQUIRE(numbers);
    size_t owner = script_numeric_usage_read(numbers).retained_bytes;
    script_numeric_limits limited = fixture_limits;
    limited.storage = owner;
    REQUIRE(restart(limited));
    REQUIRE(!integer("1"));
    REQUIRE(error.code == SCRIPT_NUMERIC_STORAGE);
    REQUIRE(script_numeric_usage_read(numbers).retained_bytes == owner);
    REQUIRE(script_numeric_usage_read(numbers).published_values == 0);
    limited.storage = owner - 1;
    cleanup();
    numbers = script_numeric_new(&limited, &error);
    REQUIRE(!numbers && error.code == SCRIPT_NUMERIC_STORAGE);
    return true;
}
TEST_CASE(numeric_rejects_non_tokens_without_syntax_extension)
{
    static const struct
    {
        const char *text;
        script_number_kind kind;
    } rows[] = {
        { "", SCRIPT_NUMBER_INTEGER },    { ".5", SCRIPT_NUMBER_REAL },
        { "1.", SCRIPT_NUMBER_REAL },     { "1f", SCRIPT_NUMBER_INTEGER },
        { "0.1f", SCRIPT_NUMBER_REAL },   { "0x", SCRIPT_NUMBER_INTEGER },
        { "1e", SCRIPT_NUMBER_REAL },     { "1e+", SCRIPT_NUMBER_REAL },
        { "1e2", SCRIPT_NUMBER_INTEGER }, { "12", SCRIPT_NUMBER_REAL },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof *rows; i++) {
        REQUIRE(!script_number_read(
            numbers,
            rows[i].text,
            strlen(rows[i].text),
            rows[i].kind,
            &error
        ));
        REQUIRE(error.code == SCRIPT_NUMERIC_LITERAL);
    }
    REQUIRE(expect_integer(integer("0xff"), PIN_U8, 255));
    return true;
}
TEST_CASE(numeric_exponent_is_bounded_before_power_expansion)
{
    REQUIRE(!real("1e999999"));
    REQUIRE(error.code == SCRIPT_NUMERIC_BITS);
    REQUIRE(script_numeric_usage_read(numbers).published_values == 0);
    REQUIRE(restart(fixture_limits));
    REQUIRE(!real("0e999999"));
    REQUIRE(error.code == SCRIPT_NUMERIC_BITS);
    REQUIRE(script_numeric_usage_read(numbers).published_values == 0);
    return true;
}
void test_numeric_exact_core()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(numeric_kind_and_decimal_scale);
    RUN_TEST_CASE(numeric_all_integer_storage_boundaries);
    RUN_TEST_CASE(numeric_preserves_wide_integer_intermediates);
    RUN_TEST_CASE(numeric_real_rounds_once_to_target);
    RUN_TEST_CASE(numeric_contextual_integral_real_is_checked);
    RUN_TEST_CASE(numeric_division_depends_on_operand_kind);
    RUN_TEST_CASE(numeric_integer_division_and_remainder_signs);
    RUN_TEST_CASE(numeric_exact_rationals_cancel);
    RUN_TEST_CASE(numeric_real_zero_sign_is_a_value_fact);
    RUN_TEST_CASE(numeric_subnormal_and_halfway_rounding);
    RUN_TEST_CASE(numeric_subnormal_carry_reaches_normal);
    RUN_TEST_CASE(numeric_finite_overflow_and_signed_underflow);
    RUN_TEST_CASE(numeric_exact_bitwise_and_shifts);
    RUN_TEST_CASE(numeric_shift_count_has_a_finite_limit);
    RUN_TEST_CASE(numeric_bit_expansion_stops_before_capacity);
    RUN_TEST_CASE(numeric_no_unsupported_operators_are_inferred);
    RUN_TEST_CASE(numeric_materialization_never_mutates_shared_facts);
    RUN_TEST_CASE(numeric_step_and_depth_budgets_are_shared);
    RUN_TEST_CASE(numeric_work_is_reserved_before_input_and_arithmetic);
    RUN_TEST_CASE(numeric_storage_includes_scratch_and_arena_capacity);
    RUN_TEST_CASE(numeric_rejects_non_tokens_without_syntax_extension);
    RUN_TEST_CASE(numeric_exponent_is_bounded_before_power_expansion);
    pu_clear_setup();
}
