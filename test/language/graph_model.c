#include "graph/model/behavior_asset.h"
#include "graph/model/value_copy.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/text.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *label;
} nested_value;
typedef struct
{
    nested_value nested;
    int32_t count;
} graph_value;
static const type_field nested_field = { .name = "label",
                                         .type = PIN_STR,
                                         .type_name = "const char *",
                                         .size = sizeof(char *) };
static const type_desc nested_type = { .name = "fixture.graph.nested",
                                       .size = sizeof(nested_value),
                                       .fields = &nested_field,
                                       .field_count = 1 };
static const type_field value_fields[] = {
    { .name = "nested",
      .type = PIN_STRUCT,
      .type_name = "fixture.graph.nested",
      .size = sizeof(nested_value) },
    { .name = "count",
      .type = PIN_I32,
      .type_name = "int",
      .offset = offsetof(graph_value, count),
      .size = sizeof(int32_t) },
};
static const type_desc value_type = { .name = "fixture.graph.value",
                                      .size = sizeof(graph_value),
                                      .fields = value_fields,
                                      .field_count = 2 };
static const type_desc *const graph_types[] = { &nested_type, &value_type };
type_catalog_pack test_graph_model_types()
{
    return (type_catalog_pack){ .types = graph_types, .type_count = 2 };
}
static behavior_asset *asset;
static char *producer;
static int retired;
static void cleanup()
{
    behavior_asset_free(asset);
    asset = NULL;
    free(producer);
    producer = NULL;
}
typedef struct
{
    behavior_asset *owner;
    uint64_t node_id;
} artifact;
static void retire(void *payload)
{
    artifact *value = payload;
    assert(graph_node_by_id(value->owner->graph, value->node_id));
    retired++;
    free(value);
}
static artifact *artifact_new(uint64_t node_id)
{
    artifact *value = malloc(sizeof *value);
    assert(value);
    *value = (artifact){ asset, node_id };
    return value;
}
TEST_CASE(graph_model_owns_nested_values_and_artifact_lifetimes)
{
    asset = behavior_asset_new("Owned");
    const node_desc descriptor = { .name = "Fixture" };
    node *n = graph_add_node(asset->graph, &descriptor);
    REQUIRE(n != NULL);
    producer = malloc(32);
    REQUIRE(producer != NULL);
    strcpy(producer, "nested owned label");
    graph_value original = { .nested = { producer }, .count = 17 };
    pin_value borrowed = { .type = PIN_STRUCT,
                           .type_name = value_type.name,
                           .ptr = &original };
    pin_value snapshot;
    REQUIRE(pin_value_copy_to_graph(asset->graph, &snapshot, &borrowed) == 0);
    original.count = 99;
    memset(producer, 'x', strlen(producer));
    free(producer);
    producer = NULL;
    const graph_value *owned = snapshot.ptr;
    REQUIRE(
        snapshot.type == PIN_STRUCT && !strcmp(snapshot.type_name, value_type.name)
    );
    REQUIRE(owned != &original && owned->count == 17);
    REQUIRE(!strcmp(owned->nested.label, "nested owned label"));
    pin_value items[] = { { .type = PIN_STR,
                            .text = &(const pin_string){
                                .bytes = owned->nested.label,
                                .length = (uint32_t)strlen(owned->nested.label) } } };
    pin_array source = { .element_type = PIN_STR, .count = 1, .capacity = 1, .items = items };
    pin_value copy,
        header = { .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = &source };
    REQUIRE(!pin_value_copy_to_graph(asset->graph, &copy, &header));
    const pin_array *array = copy.ptr;
    REQUIRE(array && array != &source && array->items != items);
    items[0].text = &(const pin_string){ .bytes = "changed",
                                         .length = (uint32_t)strlen("changed") };
    REQUIRE(
        pin_array_get(array, 0).type == PIN_STR &&
        !strcmp(pin_text(pin_array_get(array, 0)).bytes, "nested owned label")
    );
    retired = 0;
    artifact *first = artifact_new(n->id);
    behavior_asset_set_artifact(asset, first, retire);
    REQUIRE(behavior_asset_artifact(asset) == first);
    behavior_asset_set_artifact(asset, first, retire);
    REQUIRE(retired == 0);
    behavior_asset_set_artifact(asset, artifact_new(n->id), retire);
    REQUIRE(retired == 1);
    behavior_asset_free(asset);
    asset = NULL;
    REQUIRE(retired == 2);
    return true;
}
TEST_CASE(graph_model_topology_revisions_and_guards_are_explicit)
{
    asset = behavior_asset_new("Topology");
    pin_spec input = { .name = "Exec", .kind = PIN_EXEC, .type = PIN_FLOW };
    pin_spec output = { .name = "Then", .kind = PIN_EXEC, .type = PIN_FLOW };
    const node_desc descriptor = { .name = "Fixture",
                                   .inputs = &input,
                                   .input_count = 1,
                                   .outputs = &output,
                                   .output_count = 1 };
    node *a = graph_add_node(asset->graph, &descriptor);
    node *b = graph_add_node(asset->graph, &descriptor);
    REQUIRE(a && b && a->id != b->id);
    REQUIRE(a->input_pins[0].id != b->input_pins[0].id);
    uint64_t revision = graph_topology_revision(asset->graph);
    REQUIRE(graph_connect_exec(asset->graph, a, 0, b, 0, 0) == 0);
    REQUIRE(graph_topology_revision(asset->graph) > revision);
    node *target = NULL;
    int target_pin = -1;
    REQUIRE(graph_exec_output_target(asset->graph, a, 0, 0, &target, &target_pin) == 1);
    REQUIRE(target == b && target_pin == 0);
    REQUIRE(graph_exec_input_count(asset->graph, b, 0) == 1);
    revision = graph_topology_revision(asset->graph);
    graph_flow_pause(asset->graph);
    REQUIRE(graph_add_node(asset->graph, &descriptor) == NULL);
    REQUIRE(graph_topology_revision(asset->graph) == revision);
    graph_flow_resume(asset->graph);
    uint64_t id = b->id;
    REQUIRE(graph_remove_node(asset->graph, b) == 0);
    REQUIRE(graph_node_by_id(asset->graph, id) == NULL);
    REQUIRE(graph_exec_output_target(asset->graph, a, 0, 0, &target, &target_pin) == 0);
    REQUIRE(graph_topology_revision(asset->graph) > revision);
    return true;
}
void test_graph_model()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(graph_model_owns_nested_values_and_artifact_lifetimes);
    RUN_TEST_CASE(graph_model_topology_revisions_and_guards_are_explicit);
    pu_clear_setup();
}
