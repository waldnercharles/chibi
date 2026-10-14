#include "runtime/value_json.h"

#include "execution/function.h"
#include "runtime/value_json_native.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static char *text, *large;
static arena storage;
static bool project_types;
static void cleanup()
{
    free(text);
    free(large);
    text = large = NULL;
    arena_free(&storage);
    if (project_types) {
        type_catalog_set_project(NULL);
        project_types = false;
    }
}

TEST_CASE(json_encodes_owned_unicode_and_empty_arrays)
{
    char error[256];
    pin_value value = { .type = PIN_STR, .str = "<tag>\"\\\n界" };
    REQUIRE(value_to_json(&value, &text, error, sizeof error));
    REQUIRE(!strcmp(text, "\"<tag>\\\"\\\\\\n界\""));
    free(text);
    text = NULL;
    value = (pin_value){ .type = PIN_STRUCT,
                         .type_name = "Absent",
                         .container = PIN_CONTAINER_ARRAY };
    REQUIRE(value_to_json(&value, &text, error, sizeof error));
    REQUIRE(!strcmp(text, "[]"));
    return true;
}

TEST_CASE(json_encodes_reflected_record_arrays_without_losing_types)
{
    typedef struct
    {
        int start, end;
        const char *kind;
    } token;
    static const type_field fields[] = { { .name = "start",
                                           .type = PIN_I32,
                                           .offset = offsetof(token, start),
                                           .size = sizeof(int) },
                                         { .name = "end",
                                           .type = PIN_I32,
                                           .offset = offsetof(token, end),
                                           .size = sizeof(int) },
                                         { .name = "kind",
                                           .type = PIN_STR,
                                           .offset = offsetof(token, kind),
                                           .size = sizeof(const char *) } };
    static const type_desc type = { .name = "JsonTestToken",
                                    .size = sizeof(token),
                                    .fields = fields,
                                    .field_count = 3 };
    static const type_desc *const types[] = { &type };
    static const type_catalog_pack pack = { .types = types, .type_count = 1 };
    type_catalog_set_project(&pack);
    project_types = true;
    token record = { 0, 5, "tok-name" };
    pin_value item = { .type = PIN_STRUCT, .type_name = "JsonTestToken", .ptr = &record };
    pin_array array = { .count = 1, .capacity = 1, .items = &item };
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = "JsonTestToken",
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = &array };
    char error[256];
    REQUIRE(value_to_json(&value, &text, error, sizeof error));
    REQUIRE(!strcmp(text, "[{\"start\":0,\"end\":5,\"kind\":\"tok-name\"}]"));
    return true;
}

TEST_CASE(json_refuses_nonfinite_tasks_maps_and_unbounded_values)
{
    char error[256];
    pin_value value = { .type = PIN_F64, .d = NAN };
    REQUIRE(
        !value_to_json(&value, &text, error, sizeof error) && !text &&
        strstr(error, "finite")
    );
    value = (pin_value){ .type = PIN_TASK, .u64 = 123 };
    REQUIRE(!value_to_json(&value, &text, error, sizeof error) && strstr(error, "tasks"));
    value = (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_MAP };
    REQUIRE(!value_to_json(&value, &text, error, sizeof error) && strstr(error, "maps"));
    large = xmalloc(VALUE_JSON_MAX_BYTES + 1);
    memset(large, 'a', VALUE_JSON_MAX_BYTES);
    large[VALUE_JSON_MAX_BYTES] = 0;
    value = (pin_value){ .type = PIN_STR, .str = large };
    REQUIRE(!value_to_json(&value, &text, error, sizeof error) && strstr(error, "budget"));
    pin_array cycle = { .count = 1, .capacity = 1, .items = &value };
    value = (pin_value){ .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = &cycle };
    REQUIRE(!value_to_json(&value, &text, error, sizeof error) && strstr(error, "nesting"));
    return true;
}

static const char *copy_result(void *opaque, const char *text)
{
    arena *storage = opaque;
    size_t length = strlen(text) + 1;
    char *copy = arena_alloc(storage, length);
    memcpy(copy, text, length);
    return copy;
}

TEST_CASE(json_native_results_own_their_text_and_report_typed_refusals)
{
    pin_value in = { .type = PIN_STR, .str = "kept" }, out[3] = { 0 };
    behavior_function_context context = { .value_storage = &storage,
                                          .copy_string = copy_result };
    REQUIRE(!VALUE_JSON_CALL.context(&context, NULL, &in, out));
    REQUIRE(out[1].b && !strcmp(out[0].str, "\"kept\"") && !strcmp(out[2].str, ""));
    const char *retained = out[0].str;
    in = (pin_value){ .type = PIN_F64, .d = INFINITY };
    REQUIRE(!VALUE_JSON_CALL.context(&context, NULL, &in, out));
    REQUIRE(!out[1].b && !strcmp(out[0].str, "") && strstr(out[2].str, "finite"));
    REQUIRE(!strcmp(retained, "\"kept\""));
    return true;
}

void test_value_json()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(json_encodes_owned_unicode_and_empty_arrays);
    RUN_TEST_CASE(json_encodes_reflected_record_arrays_without_losing_types);
    RUN_TEST_CASE(json_refuses_nonfinite_tasks_maps_and_unbounded_values);
    RUN_TEST_CASE(json_native_results_own_their_text_and_report_typed_refusals);
    pu_clear_setup();
}
