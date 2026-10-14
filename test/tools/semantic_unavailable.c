#include "authoring/document_graph_definition.h"
#include "authoring/document_layout_definition.h"
#include "authoring/document_observation_definition.h"
#include "authoring/document_port_schema_definition.h"
#include "authoring/document_semantics_definition.h"
#include "authoring/document_source_inventory_definition.h"
#include "authoring/project_semantics_definition.h"
#include "execution/function.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <string.h>

static type_catalog_pack previous;
static bool had_previous;
static const type_desc **types;
static arena storage;
static void setup(void)
{
    const type_catalog_pack *old = type_catalog_project();
    had_previous = old != NULL;
    if (old) {
        previous = *old;
        type_catalog_set_project(NULL);
    }
    const type_catalog_pack *packs[] = { &DOCUMENT_SEMANTICS_TYPE_PACK,
                                         &DOCUMENT_GRAPH_TYPE_PACK,
                                         &PROJECT_SEMANTICS_TYPE_PACK,
                                         &DOCUMENT_SOURCE_INVENTORY_TYPE_PACK,
                                         &DOCUMENT_PORT_SCHEMA_TYPE_PACK };
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < packs[i]->type_count; j++) {
            apush(types, packs[i]->types[j]);
        }
    }
    type_catalog_pack pack = { .types = types, .type_count = acount(types) };
    type_catalog_set_project(&pack);
}
static void cleanup(void)
{
    arena_free(&storage);
    type_catalog_set_project(NULL);
    if (had_previous) {
        type_catalog_set_project(&previous);
    }
    afree(types);
}
static bool unavailable(const node_host_call *call, pin_value *in, int error)
{
    REQUIRE(call->optional_service && call->context);
    behavior_function_context context = { .value_storage = &storage };
    pin_value out[16] = { 0 };
    REQUIRE(!call->context(&context, NULL, in, out));
    REQUIRE(out[error].str && strstr(out[error].str, "unavailable"));
    return true;
}
TEST_CASE(semantic_native_workspace_services_are_optional_and_fail_explicitly_without_a_project)
{
    pin_value in[16] = { 0 };
    for (int i = 2; i < 8; i++) {
        in[i].str = "1";
    }
    for (int i = 0; i < 8; i++) {
        REQUIRE(unavailable(
            &DOCUMENT_SEMANTICS_CALLS[i],
            in,
            i == 0               ? 6
            : (i == 1 || i == 6) ? 3
                                 : 1
        ));
    }
    for (int i = 0; i < 15; i++) {
        REQUIRE(unavailable(&DOCUMENT_GRAPH_CALLS[i], in, 1));
    }
    for (int i = 0; i < 4; i++) {
        REQUIRE(unavailable(&PROJECT_SEMANTICS_CALLS[i], in, 2));
    }
    for (int i = 0; i < 2; i++) {
        REQUIRE(unavailable(&DOCUMENT_SOURCE_INVENTORY_CALLS[i], in, 2));
    }
    for (int i = 0; i < 2; i++) {
        REQUIRE(unavailable(&DOCUMENT_PORT_SCHEMA_CALLS[i], in, i ? 1 : 3));
    }
    REQUIRE(unavailable(&DOCUMENT_LAYOUT_CALL, in, 1));
    REQUIRE(unavailable(&DOCUMENT_OBSERVATION_CALL, in, 1));
    return true;
}
void test_semantic_unavailable(void)
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(
        semantic_native_workspace_services_are_optional_and_fail_explicitly_without_a_project
    );
    pu_setup(NULL, NULL);
}
