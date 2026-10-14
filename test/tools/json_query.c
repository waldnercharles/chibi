#include "runtime/json_query.h"

#include "execution/function.h"
#include "runtime/json_query_native.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

static json_query_result result;
static char *large;
static arena storage;
static void cleanup()
{
    json_query_result_free(&result);
    free(large);
    large = NULL;
    arena_free(&storage);
}
TEST_CASE(json_query_reads_pointer_escapes_and_owned_values)
{
    char text[] = "{\"a/b\":{\"~\":[\"hello 界\",true,17,null]}}";
    char error[256];
    REQUIRE(query_json(text, "/a~1b/~0/0", &result, error, sizeof error));
    REQUIRE(result.found && !strcmp(result.kind, "string") && result.count == 9);
    REQUIRE(!strcmp(result.string, "hello 界"));
    REQUIRE(!strcmp(result.json, "\"hello 界\""));
    memset(text, 'x', sizeof text - 1);
    REQUIRE(!strcmp(result.string, "hello 界"));
    REQUIRE(query_json("{\"items\":[true,17,null]}", "/items/1", &result, error, sizeof error));
    REQUIRE(!strcmp(result.kind, "integer") && result.number == 17);
    REQUIRE(!strcmp(result.json, "17"));
    REQUIRE(query_json("[true,17,null]", "/0", &result, error, sizeof error));
    REQUIRE(result.boolean && !strcmp(result.kind, "boolean"));
    REQUIRE(query_json("[true,17,null]", "/2", &result, error, sizeof error));
    REQUIRE(result.found && !strcmp(result.kind, "null"));
    return true;
}
TEST_CASE(json_query_distinguishes_missing_and_malformed_paths)
{
    char error[256];
    REQUIRE(query_json("{\"a\":[1,2]}", "", &result, error, sizeof error));
    REQUIRE(!strcmp(result.kind, "object") && result.count == 1);
    REQUIRE(query_json("{\"a\":[1,2]}", "/a", &result, error, sizeof error));
    REQUIRE(!strcmp(result.kind, "array") && result.count == 2);
    REQUIRE(query_json("{\"a\":[1,2]}", "/absent", &result, error, sizeof error));
    REQUIRE(!result.found && !strcmp(result.kind, "missing") && !strcmp(result.json, ""));
    REQUIRE(!query_json("{}", "a", &result, error, sizeof error));
    REQUIRE(!result.found && !result.json && error[0]);
    REQUIRE(!query_json("{}", "/a~2b", &result, error, sizeof error));
    REQUIRE(!query_json("{}", "/a~", &result, error, sizeof error));
    return true;
}
TEST_CASE(json_query_rejects_shadowed_keys_and_unsafe_strings)
{
    static const char *const invalid[] = { "",
                                           "{",
                                           "{\"kind\":1,\"kind\":2}",
                                           "{\"x\":{\"a\":1,\"a\":2}}",
                                           "{\"a\":\"b\\u0000c\"}",
                                           "{\"a\\u0000b\":1}",
                                           "[1e9999]",
                                           "\"\xff\"" };
    char error[256];
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        REQUIRE(!query_json(invalid[i], "", &result, error, sizeof error));
        REQUIRE(error[0] && !result.found && !result.string && !result.json);
    }
    return true;
}
TEST_CASE(json_query_enforces_depth_and_value_budgets)
{
    char error[256];
    large = xmalloc(JSON_QUERY_MAX_DEPTH * 2 + 8);
    memset(large, '[', JSON_QUERY_MAX_DEPTH + 2);
    memset(large + JSON_QUERY_MAX_DEPTH + 2, ']', JSON_QUERY_MAX_DEPTH + 2);
    large[JSON_QUERY_MAX_DEPTH * 2 + 4] = 0;
    REQUIRE(!query_json(large, "", &result, error, sizeof error));
    free(large);
    large = xmalloc(JSON_QUERY_MAX_VALUES * 2 + 3);
    large[0] = '[';
    for (int i = 0; i < JSON_QUERY_MAX_VALUES; i++) {
        large[1 + i * 2] = '0';
        large[2 + i * 2] = ',';
    }
    large[JSON_QUERY_MAX_VALUES * 2] = ']';
    large[JSON_QUERY_MAX_VALUES * 2 + 1] = 0;
    REQUIRE(!query_json(large, "", &result, error, sizeof error));
    return true;
}
static const char *copy_string(void *context, const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(context, size);
    memcpy(copy, text, size);
    return copy;
}
TEST_CASE(json_query_native_outputs_survive_parser_release)
{
    behavior_function_context context = { .value_storage = &storage,
                                          .copy_string = copy_string };
    pin_value in[] = { { .type = PIN_STR, .str = "{\"text\":\"owned\"}" },
                       { .type = PIN_STR, .str = "/text" } },
              out[9] = { 0 };
    REQUIRE(!JSON_QUERY_CALL.context(&context, NULL, in, out));
    REQUIRE(out[0].b && out[1].b);
    REQUIRE(!strcmp(out[2].str, "string") && !strcmp(out[3].str, "owned"));
    REQUIRE(!strcmp(out[6].str, "\"owned\"") && !strcmp(out[8].str, ""));
    return true;
}
void test_json_query()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(json_query_reads_pointer_escapes_and_owned_values);
    RUN_TEST_CASE(json_query_distinguishes_missing_and_malformed_paths);
    RUN_TEST_CASE(json_query_rejects_shadowed_keys_and_unsafe_strings);
    RUN_TEST_CASE(json_query_enforces_depth_and_value_budgets);
    RUN_TEST_CASE(json_query_native_outputs_survive_parser_release);
    pu_clear_setup();
}
