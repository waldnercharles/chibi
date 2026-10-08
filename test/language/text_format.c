#include "runtime/text_format.h"

#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <string.h>

static arena storage;
static void setup()
{
    arena_init(&storage, 4096);
}
static void teardown()
{
    arena_free(&storage);
}

TEST_CASE(native_text_formats_scalar_families_and_owns_results)
{
    char source[] = "alphabet";
    pin_value inputs[] = {
        { .type = PIN_STR, .str = "{0:.2f}|{1:x}|{2:<5.3s}|{{ok}}|%" },
        { .type = PIN_F64, .d = 3.14159265 },
        { .type = PIN_U64, .u64 = UINT64_MAX },
        { .type = PIN_STR, .str = source },
    };
    pin_value output = { 0 };
    REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, inputs, 4, &output));
    memset(source, 'z', sizeof source - 1);
    REQUIRE(output.type == PIN_STR && !strcmp(output.type_name, "const char *"));
    REQUIRE(!strcmp(output.str, "3.14|ffffffffffffffff|alp  |{ok}|%"));
    pin_value equal[] = {
        { .type = PIN_STR, .str = output.str },
        { .type = PIN_STR, .str = "3.14|ffffffffffffffff|alp  |{ok}|%" },
    };
    pin_value result = { 0 };
    REQUIRE(!NATIVE_TEXT_EQUAL_CALL.value(&storage, equal, 2, &result));
    REQUIRE(result.type == PIN_BOOL && result.b);
    equal[1].str = "different";
    REQUIRE(!NATIVE_TEXT_EQUAL_HOST.value(&storage, equal, 2, &result));
    REQUIRE(!result.b);
    pin_value items[] = { { .type = PIN_STR, .str = "a" },
                          { .type = PIN_STR, .str = "b" } };
    pin_array array = { .element_type = PIN_STR, .count = 2, .capacity = 2, .items = items };
    pin_value aggregate[] = {
        { .type = PIN_STR, .str = "{}" },
        { .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = &array },
    };
    REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, aggregate, 2, &result));
    array.count = 0;
    REQUIRE(!strcmp(result.str, "Array[2]"));
    return true;
}

TEST_CASE(native_text_rejects_unsafe_modifiers_and_bounds_placeholder_indices)
{
    static const char *const formats[] = {
        "{0:n}",
        "{0:s}",
        "{0:p}",
        "{0:hn}",
        "{0:llx}",
        "{0:2$d}",
        "{0:*d}",
        "{0:999999999999999d}",
        "{0:.999999999999999d}",
        "{0:%n}",
        "{0:zc}",
        "{0:#d}",
        "{0:.2c}",
    };
    for (int i = 0; i < (int)(sizeof formats / sizeof *formats); i++) {
        pin_value inputs[] = { { .type = PIN_STR, .str = formats[i] },
                               { .type = PIN_I32, .i = 37 } };
        pin_value result = { 0 };
        REQUIRE(!NATIVE_FORMAT_TEXT_HOST.value(&storage, inputs, 2, &result));
        REQUIRE(result.type == PIN_STR && !strcmp(result.str, "37"));
    }
    pin_value inputs[] = {
        { .type = PIN_STR, .str = "a{999999999999999999999999999}b{0}c" },
        { .type = PIN_STR, .str = "safe" },
    };
    pin_value result = { 0 };
    REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, inputs, 2, &result));
    REQUIRE(!strcmp(result.str, "absafec"));
    inputs[0].str = "{0:d}";
    inputs[1].str = "text";
    REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, inputs, 2, &result));
    REQUIRE(!strcmp(result.str, "text"));
    char percents[129];
    memset(percents, '%', sizeof percents - 1);
    percents[sizeof percents - 1] = 0;
    inputs[0].str = percents;
    REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, inputs, 1, &result));
    REQUIRE(!strcmp(result.str, percents));
    return true;
}

void test_language_text_format()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(native_text_formats_scalar_families_and_owns_results);
    RUN_TEST_CASE(native_text_rejects_unsafe_modifiers_and_bounds_placeholder_indices);
    pu_clear_setup();
}
