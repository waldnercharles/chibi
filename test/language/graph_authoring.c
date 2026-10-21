#include "graph/authoring/constant.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

static behavior_asset *asset;
static arena storage;
static const registry_pack **previous_packs;
static bool registry_changed;
static void cleanup()
{
    behavior_asset_free(asset);
    asset = NULL;
    arena_free(&storage);
    if (registry_changed) {
        registry_set_packs(previous_packs, acount(previous_packs));
        registry_changed = false;
    }
    afree(previous_packs);
}
static pin_value multiply(const pin_value *inputs, int count, arena *a)
{
    (void)a;
    if (count != 2) {
        return (pin_value){ 0 };
    }
    return (pin_value){ .type = PIN_F64, .d = inputs[0].d * inputs[1].d };
}
static pin_spec inputs[] = {
    { .name = "left",
      .kind = PIN_DATA,
      .type = PIN_F64,
      .default_value = { .type = PIN_F64, .d = 2.5 } },
    { .name = "right",
      .kind = PIN_DATA,
      .type = PIN_F64,
      .default_value = { .type = PIN_F64, .d = 4.0 } },
};
static pin_spec output = { .name = "product", .kind = PIN_DATA, .type = PIN_F64 };
static pin_binding local_binding = { .kind = PIN_BINDING_LOCAL };
static node_desc product = {
    .name = "fixture product",
    .inputs = inputs,
    .input_count = 2,
    .outputs = &output,
    .output_count = 1,
    .pure_kind = NODE_PURE_CALL,
    .fn = multiply,
    .foldable = true,
};
static node_desc *const nodes[] = { &product };
static const registry_pack pack = { .name = "fixture constants",
                                    .nodes = nodes,
                                    .node_count = 1 };
static const registry_pack *const packs[] = { &pack };
TEST_CASE(graph_authoring_constant_uses_authored_defaults_and_refuses_runtime_inputs)
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    registry_set_packs(packs, 1);
    registry_changed = true;
    REQUIRE(registry_pack_count() == 1 && registry_pack_at(0) == &pack);
    REQUIRE(registry_node_count() == 1 && registry_node(0) == &product);
    REQUIRE(registry_node_by_name("fixture product") == &product);
    REQUIRE(!registry_node_by_name("missing product") && !registry_node(1));
    REQUIRE(registry_service_count() == 0);
    asset = behavior_asset_new("constants");
    node *n = graph_add_node(asset->graph, &product);
    REQUIRE(n);
    pin_value value = { 0 };
    REQUIRE(graph_constant_value(asset->graph, n, &storage, &value));
    REQUIRE(value.type == PIN_F64 && value.d == 10.0);
    n->input_pins[0].default_value.d = 3.0;
    REQUIRE(graph_constant_value(asset->graph, n, &storage, &value));
    REQUIRE(value.d == 12.0);
    n->input_pins[0].binding = &local_binding;
    value.d = 91.0;
    REQUIRE(!graph_constant_value(asset->graph, n, &storage, &value));
    REQUIRE(value.d == 91.0);
    n->input_pins[0].binding = NULL;
    n->input_pins[0].flags |= PIN_FLAG_DEFAULT_SELF;
    REQUIRE(!graph_constant_value(asset->graph, n, &storage, &value));
    return true;
}
void test_graph_authoring()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(graph_authoring_constant_uses_authored_defaults_and_refuses_runtime_inputs);
    pu_clear_setup();
}
