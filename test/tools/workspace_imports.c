#include "authoring/workspace_imports_definition.h"
#include "execution/function.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <string.h>
TEST_CASE(import_absent_service_refuses_all_core_operations)
{
    char error[128];
    workspace_import_view view = { .plan = 99, .committed = true, .action_count = 99 };
    REQUIRE(!workspace_import_prepare(NULL, "", "", false, false, error, sizeof error));
    REQUIRE(strstr(error, "unavailable"));
    REQUIRE(!workspace_import_read(NULL, 1, &view));
    REQUIRE(!view.plan && !view.committed && !view.action_count && !view.actions && !view.report);
    REQUIRE(strstr(view.error, "unavailable"));
    REQUIRE(!workspace_import_apply(NULL, 1, error, sizeof error));
    REQUIRE(strstr(error, "unavailable"));
    REQUIRE(!workspace_import_close(NULL, 1));
    workspace_imports_free(NULL);
    return true;
}
TEST_CASE(import_optional_calls_have_explicit_empty_unavailable_results)
{
    arena storage;
    arena_init(&storage, 4096);
    behavior_function_context context = { .value_storage = &storage };
    pin_value in[4] = { { 0 } }, out[9] = { { 0 } };
    const native_definition_pack *pack = &WORKSPACE_IMPORTS_DEFINITION_PACK;
    REQUIRE(pack->binding_count == 4);
    for (int i = 0; i < 4; i++) {
        const node_host_call *call = pack->bindings[i].implementation.call;
        REQUIRE(call && call->optional_service && call->context);
        out[0].i = 99;
        out[5].i = 99;
        out[6].i = 99;
        out[7].i = 99;
        out[8].i = 99;
        REQUIRE(!call->context(&context, NULL, in, out));
        if (i == 0) {
            REQUIRE(out[0].i == 0);
            REQUIRE(strstr(out[1].str, "unavailable"));
        }
        if (i == 1) {
            REQUIRE(!out[0].b && !out[1].b);
            REQUIRE(strstr(out[2].str, "unavailable"));
            REQUIRE(((pin_array *)out[3].ptr)->count == 0);
            REQUIRE(((pin_array *)out[4].ptr)->count == 0);
            REQUIRE(!out[5].i && !out[6].i && !out[7].i && !out[8].i);
        }
        if (i == 2) {
            REQUIRE(!out[0].b && !out[1].b);
            REQUIRE(strstr(out[2].str, "unavailable"));
        }
        if (i == 3) {
            REQUIRE(!out[0].b);
            REQUIRE(strstr(out[1].str, "unavailable"));
        }
    }
    arena_free(&storage);
    return true;
}
TEST_SUITE(test_tool_workspace_imports)
{
    RUN_TEST_CASE(import_absent_service_refuses_all_core_operations);
    RUN_TEST_CASE(import_optional_calls_have_explicit_empty_unavailable_results);
}
