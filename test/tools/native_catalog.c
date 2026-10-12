#include "execution/function.h"
#include "host/native_default.h"
#include "runtime/components.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

void test_native_catalog_enable();
static native_capabilities *native;
static arena values;
static void cleanup()
{
    native_capabilities_free(native);
    native = NULL;
    arena_free(&values);
}
TEST_CASE(native_application_catalog_links_generated_kernels_and_input_reads)
{
    test_native_catalog_enable();
    arena_init(&values, 4096);
    char error[256] = { 0 };
    native = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    if (!native) {
        fprintf(stderr, "native application catalog: %s\n", error);
    }
    REQUIRE(native);
    const script_catalog *catalog = native_capabilities_catalog(native);
    int index = script_catalog_find_capability(catalog, "move_transform", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(index >= 0);
    const script_catalog_capability *declaration = &catalog->capabilities[index];
    const native_capability_binding *binding = native_capabilities_find(
        native,
        declaration->id,
        script_catalog_capability_abi(catalog, declaration)
    );
    REQUIRE(binding && binding->call && binding->call->context);
    Transform input = { .x = 2, .y = 3 }, output = { 0 };
    const pin_value arguments[] = {
        { .type = PIN_STRUCT, .type_name = "Transform", .ptr = &input },
        { .type = PIN_F32, .f = -5 },
        { .type = PIN_F32, .f = 7 },
    };
    pin_value result = { .type = PIN_STRUCT, .type_name = "Transform", .ptr = &output };
    behavior_function_context context = { .value_storage = &values };
    REQUIRE(!binding->call->context(&context, NULL, arguments, &result));
    input = (Transform){ 99, 99 };
    REQUIRE(output.x == -3 && output.y == 10);
    REQUIRE(result.type == PIN_STRUCT && !strcmp(result.type_name, "Transform"));
    /* Actions are read by name; the input map declares their events. */
    index = script_catalog_find_capability(catalog, "Action Held", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(index >= 0);
    declaration = &catalog->capabilities[index];
    REQUIRE(declaration->parameter_count == 1 && declaration->parameters[0].type.storage == PIN_STR);
    index = script_catalog_find_capability(catalog, "get_device_state", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(index >= 0);
    declaration = &catalog->capabilities[index];
    REQUIRE(declaration->result_count == 7);
    REQUIRE(!strcmp(declaration->results[0].name, "Width"));
    REQUIRE(!strcmp(declaration->results[5].name, "Tilt"));
    const char *device_actions[] = {
        "request_device_motion",
        "disable_device_motion",
        "recenter_device_motion",
        "vibrate_device",
    };
    for (int i = 0; i < 4; i++) {
        REQUIRE(
            script_catalog_find_capability(catalog, device_actions[i], SCRIPT_CAPABILITY_ACTION) >= 0
        );
    }
    /* No host event takes an action's name, so a map may name Fire or Dash. */
    REQUIRE(script_catalog_find_capability(catalog, "Tick", SCRIPT_CAPABILITY_EVENT) >= 0);
    REQUIRE(script_catalog_find_capability(catalog, "Fire", SCRIPT_CAPABILITY_EVENT) < 0);
    REQUIRE(script_catalog_find_capability(catalog, "Dash", SCRIPT_CAPABILITY_EVENT) < 0);
    native_capabilities_free(native);
    native = NULL;
    REQUIRE(output.x == -3 && output.y == 10);
    return true;
}
void test_tool_native_catalog()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(native_application_catalog_links_generated_kernels_and_input_reads);
    pu_clear_setup();
}
