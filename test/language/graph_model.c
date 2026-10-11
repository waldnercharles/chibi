#include "graph/authoring/graph_edit.h"
#include "graph/compiler/validation.h"
#include "graph/document/serialize.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "graph/model/value_copy.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

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
static behavior_asset *copy;
static compile_report report;
static const registry_pack **previous_packs;
static bool registry_changed;
static char *producer;
static int retired;
static void cleanup()
{
    behavior_asset_free(asset);
    asset = NULL;
    behavior_asset_free(copy);
    copy = NULL;
    compile_report_free(&report);
    if (registry_changed) {
        registry_set_packs(previous_packs, acount(previous_packs));
        registry_changed = false;
    }
    afree(previous_packs);
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
    pin_value items[] = { { .type = PIN_STR, .str = owned->nested.label } };
    pin_array source = { .element_type = PIN_STR, .count = 1, .capacity = 1, .items = items };
    pin_array *array = pin_array_clone_to_graph(asset->graph, &source);
    REQUIRE(array && array != &source && array->items != items);
    items[0].str = "changed";
    REQUIRE(array->items[0].type == PIN_STR && !strcmp(array->items[0].str, "nested owned label"));
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

static pin_value sparse_value(const pin_value *inputs, int count, arena *storage)
{
    (void)count;
    (void)storage;
    return inputs[0];
}
static pin_spec sparse_data_pin = {
    .name = "Value",
    .kind = PIN_DATA,
    .type = PIN_F64,
    .default_value = { .type = PIN_F64 },
};
static pin_spec sparse_exec_pin = { .name = "Exec", .kind = PIN_EXEC, .type = PIN_FLOW };
static node_desc sparse_data = {
    .name = "Fixture Sparse Data",
    .inputs = &sparse_data_pin,
    .input_count = 1,
    .outputs = &sparse_data_pin,
    .output_count = 1,
    .pure_kind = NODE_PURE_CALL,
    .fn = sparse_value,
};
static node_desc sparse_exec = {
    .name = "Fixture Sparse Exec",
    .inputs = &sparse_exec_pin,
    .input_count = 1,
    .outputs = &sparse_exec_pin,
    .output_count = 1,
};
static node_desc *const sparse_nodes[] = { &sparse_data, &sparse_exec };
static const registry_pack sparse_pack = {
    .name = "Sparse IDs",
    .nodes = sparse_nodes,
    .node_count = 2,
};

TEST_CASE(graph_model_sparse_ids_preserve_lookup_editing_and_validation)
{
    asset = behavior_asset_new("Sparse");
    graph *g = asset->graph;
    g->id_counter = 50000;
    node *a = graph_add_node(g, &sparse_data);
    REQUIRE(a && a->id == 50000);
    /* Fail the dense implementation at a safe allocation before exercising
     * IDs beyond 32 bits. One node must not allocate 50,000 pointer slots. */
    REQUIRE(acap(g->id_table) <= 16);
    g->id_counter = 100000;
    node *b = graph_add_node(g, &sparse_data);
    g->id_counter = UINT64_C(1) << 40;
    node *c = graph_add_node(g, &sparse_data);
    node *d = graph_add_node(g, &sparse_exec);
    node *e = graph_add_node(g, &sparse_exec);
    node *f = graph_add_node(g, &sparse_exec);
    REQUIRE(b && c && d && e && f);
    REQUIRE(acap(g->id_table) <= 2 * acount(g->nodes) + 16);
    REQUIRE(graph_node_by_id(g, c->id) == c);
    REQUIRE(!graph_node_by_id(g, c->input_pins[0].id));
    REQUIRE(!graph_node_by_id(g, UINT64_MAX));
    REQUIRE(graph_connect_data(g, a, 0, b, 0) == 0);
    REQUIRE(graph_node_can_inline(g, a));
    REQUIRE(graph_connect_data(g, a, 0, c, 0) == 0);
    REQUIRE(!graph_node_can_inline(g, a));
    node *source = NULL;
    int pin = -1;
    REQUIRE(graph_input_source(g, c, 0, &source, &pin) == 1);
    REQUIRE(source == a && pin == 0);
    REQUIRE(graph_remove_edge(g, a, 0, c, 0) == 0);
    REQUIRE(graph_node_can_inline(g, a));
    REQUIRE(graph_input_source(g, c, 0, &source, &pin) == 0);
    REQUIRE(graph_connect_exec(g, d, 0, e, 0, 11) == 0);

    /* Loading/clone/history preserve IDs even when authored node order is not
     * numeric and constructor scratch IDs differ from the serialized IDs. */
    g->nodes[0] = f;
    g->nodes[5] = a;
    a->x = 42.5f;
    a->breakpoint = true;
    graph_reindex(g);
    for (int i = 0; i < acount(g->nodes); i++) {
        REQUIRE(graph_node_by_id(g, g->nodes[i]->id) == g->nodes[i]);
    }
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &sparse_pack };
    registry_set_packs(packs, 1);
    registry_changed = true;
    copy = behavior_asset_clone(asset);
    REQUIRE(copy);
    REQUIRE(behavior_asset_equal(asset, copy));
    REQUIRE(graph_node_by_id(copy->graph, a->id)->x == 42.5f);
    REQUIRE(graph_node_by_id(copy->graph, a->id)->breakpoint);
    REQUIRE(
        graph_input_source(copy->graph, graph_node_by_id(copy->graph, b->id), 0, &source, &pin)
    );
    REQUIRE(source->id == a->id && pin == 0);
    /* The writer sorts nodes; reorder its value explicitly to exercise an
     * unsorted authored document at the loader boundary too. */
    av_doc *document = av_doc_new();
    av_value *value = behavior_asset_to_value(document, asset);
    av_value *nodes = av_obj_get(av_arr_get(av_obj_get(value, "graphs"), 0), "nodes");
    av_value *first_node = av_arr_get(nodes, 0);
    av_value *last_node = av_arr_get(nodes, 5);
    av_arr_replace(nodes, 0, last_node);
    av_arr_replace(nodes, 5, first_node);
    behavior_asset_free(copy);
    copy = behavior_asset_from_value(value);
    av_doc_free(document);
    REQUIRE(copy);
    REQUIRE(copy->graph->nodes[0]->id == f->id);
    REQUIRE(behavior_asset_equal(asset, copy));

    asset->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    REQUIRE(graph_asset_compile(asset, &report) == 0);
    REQUIRE(graph_connect_data(g, b, 0, a, 0) == 0);
    REQUIRE(graph_asset_compile(asset, &report) > 0);
    int cycles = 0;
    for (int i = 0; i < acount(report.items); i++) {
        if (report.items[i].rule == CMPL_RULE_DATA_CYCLE) {
            REQUIRE(report.items[i].node_id == a->id || report.items[i].node_id == b->id);
            cycles++;
        }
    }
    REQUIRE(cycles == 1);
    b->disabled = true;
    REQUIRE(graph_asset_compile(asset, &report) == 0);
    b->disabled = false;
    REQUIRE(graph_remove_edge(g, b, 0, a, 0) == 0);
    REQUIRE(graph_asset_compile(asset, &report) == 0);

    /* Readers also retain authored traversal order for stored exec fan-out. */
    apush(
        g->edges,
        ((edge){ .from_id = d->id,
                 .from_pin_id = d->output_pins[0].id,
                 .to_id = f->id,
                 .to_pin_id = f->input_pins[0].id,
                 .kind = GRAPH_EDGE_EXEC,
                 .exec_order = 5 })
    );
    graph_reindex(g);
    const edge *first = NULL;
    REQUIRE(graph_exec_output_edges(g, d, 0, &first) == 2);
    REQUIRE(first && first->to_id == e->id && first->exec_order == 11);
    REQUIRE(graph_exec_output_target(g, d, 0, 5, &source, &pin) == 1);
    REQUIRE(source == f && pin == 0);
    REQUIRE(graph_exec_output_target(g, d, 0, 11, &source, &pin) == 1);
    REQUIRE(source == e && pin == 0);
    REQUIRE(graph_remove_edge(g, d, 0, e, 0) == 0);
    REQUIRE(graph_exec_output_edges(g, d, 0, &first) == 1);
    REQUIRE(first && first->to_id == f->id);
    uint64_t removed = c->id;
    REQUIRE(graph_remove_node(g, c) == 0);
    REQUIRE(!graph_node_by_id(g, removed));
    g->id_counter = 75000;
    node *middle = graph_add_node(g, &sparse_data);
    REQUIRE(middle && graph_node_by_id(g, middle->id) == middle);
    REQUIRE(graph_node_by_id(g, a->id) == a && graph_node_by_id(g, b->id) == b);
    return true;
}
void test_graph_model()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(graph_model_owns_nested_values_and_artifact_lifetimes);
    RUN_TEST_CASE(graph_model_topology_revisions_and_guards_are_explicit);
    RUN_TEST_CASE(graph_model_sparse_ids_preserve_lookup_editing_and_validation);
    pu_clear_setup();
}
