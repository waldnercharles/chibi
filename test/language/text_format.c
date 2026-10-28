#include "runtime/text_format.h"

#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/map.h"
#include "types/text.h"

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
        { .type = PIN_STR,
          .text = &(
              const pin_string
          ){ .bytes = "{0:.2f}|{1:x}|{2:<5.3s}|{{ok}}|%",
             .length = sizeof("{0:.2f}|{1:x}|{2:<5.3s}|{{ok}}|%") - 1 } },
        { .type = PIN_F64, .d = 3.14159265 },
        { .type = PIN_U64, .u64 = UINT64_MAX },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = source,
                                       .length = (uint32_t)strlen(source) } },
    };
    pin_value output = { 0 };
    REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, inputs, 4, &output));
    pin_value hosted = { 0 };
    REQUIRE(!NATIVE_FORMAT_TEXT_HOST.value(&storage, inputs, 4, &hosted));
    memset(source, 'z', sizeof source - 1);
    REQUIRE(output.type == PIN_STR && !output.type_name);
    REQUIRE(!strcmp(pin_text(output).bytes, "3.14|ffffffffffffffff|alp  |{ok}|%"));
    REQUIRE(hosted.type == PIN_STR);
    REQUIRE(!strcmp(pin_text(hosted).bytes, "3.14|ffffffffffffffff|alp  |{ok}|%"));
    pin_value equal[] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = pin_text(output).bytes,
                                       .length = (uint32_t)
                                           strlen(pin_text(output).bytes) } },
        { .type = PIN_STR,
          .text = &(
              const pin_string
          ){ .bytes = "3.14|ffffffffffffffff|alp  |{ok}|%",
             .length = sizeof("3.14|ffffffffffffffff|alp  |{ok}|%") - 1 } },
    };
    pin_value result = { 0 };
    REQUIRE(!NATIVE_TEXT_EQUAL_CALL.value(&storage, equal, 2, &result));
    REQUIRE(result.type == PIN_BOOL && result.b);
    equal[1].text = &(const pin_string){ .bytes = "different",
                                         .length = (uint32_t)strlen("different") };
    REQUIRE(!NATIVE_TEXT_EQUAL_HOST.value(&storage, equal, 2, &result));
    REQUIRE(!result.b);
    pin_value items[] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "a", .length = sizeof("a") - 1 } },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "b", .length = sizeof("b") - 1 } }
    };
    pin_array array = { .element_type = PIN_STR, .count = 2, .capacity = 2, .items = items };
    pin_value aggregate[] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "{}", .length = sizeof("{}") - 1 } },
        { .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = &array },
    };
    REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, aggregate, 2, &result));
    array.count = 0;
    REQUIRE(!strcmp(pin_text(result).bytes, "Array[2]"));
    return true;
}

TEST_CASE(native_text_formats_maps_with_their_entry_counts)
{
    pin_value key_type = { .type = PIN_STR }, value_type = { .type = PIN_I32 };
    pin_map_entry entry = {
        .key = { .type = PIN_STR,
                 .text = &(const pin_string){ .bytes = "Ada",
                                              .length = sizeof("Ada") - 1 } },
        .value = { .type = PIN_I32, .i = 1 }
    };
    pin_map *map = pin_map_create(&storage, key_type, value_type, &entry, 1);
    pin_map *empty = pin_map_create(&storage, key_type, value_type, NULL, 0);
    REQUIRE(map && empty);
    pin_value inputs[] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = "{}|{}|{}",
                                       .length = sizeof("{}|{}|{}") - 1 } },
        { .type = PIN_I32, .container = PIN_CONTAINER_MAP, .ptr = map },
        { .type = PIN_I32, .container = PIN_CONTAINER_MAP, .ptr = empty },
        { .type = PIN_I32, .container = PIN_CONTAINER_MAP },
    };
    pin_value output = { 0 };
    REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, inputs, 4, &output));
    REQUIRE(output.type == PIN_STR);
    REQUIRE(!strcmp(pin_text(output).bytes, "Map[1]|Map[0]|Map[0]"));
    return true;
}

void test_language_text_format()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(native_text_formats_scalar_families_and_owns_results);
    RUN_TEST_CASE(native_text_formats_maps_with_their_entry_counts);
    pu_clear_setup();
}
