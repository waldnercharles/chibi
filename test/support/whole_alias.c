#include "graph/compiler/behavior_program.h"
#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "test/support/report.h"
#include "test/whole_alias_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    float x, y;
} alias_vector;
static const type_field fields[] = {
    { "x", PIN_F32, offsetof(alias_vector, x), sizeof(float), "float" },
    { "y", PIN_F32, offsetof(alias_vector, y), sizeof(float), "float" },
};
static const type_desc source_type = { .name = "AliasSource",
                                       .size = sizeof(alias_vector),
                                       .fields = fields,
                                       .field_count = 2 };
static const type_desc target_type = { .name = "AliasTarget",
                                       .size = sizeof(alias_vector),
                                       .fields = fields,
                                       .field_count = 2,
                                       .alias_of = "AliasSource" };
static pin_value copy_alias(const pin_value *args, arena *storage, const char *name)
{
    pin_value result = { .type = PIN_STRUCT, .type_name = name };
    if (args[0].ptr) {
        result.ptr = arena_alloc(storage, sizeof(alias_vector));
        memcpy(result.ptr, args[0].ptr, sizeof(alias_vector));
    }
    return result;
}
static pin_value to_alias(const pin_value *args, int, arena *storage)
{
    return copy_alias(args, storage, "AliasTarget");
}
static pin_value from_alias(const pin_value *args, int, arena *storage)
{
    return copy_alias(args, storage, "AliasSource");
}
static pin_spec source_pin = { .name = "Value", .type = PIN_STRUCT, .type_name = "AliasSource" };
static pin_spec target_pin = { .name = "Value", .type = PIN_STRUCT, .type_name = "AliasTarget" };
static node_desc conversions[] = {
    { .name = "To Alias",
      .description = "Converts an AliasSource to the AliasTarget it aliases.",
      .pure_kind = NODE_PURE_CALL,
      .jit_kind = NODE_JIT_IDENTITY,
      .inputs = &source_pin,
      .input_count = 1,
      .outputs = &target_pin,
      .output_count = 1,
      .conversion_from = "AliasSource",
      .conversion_to = "AliasTarget",
      .fn = to_alias },
    { .name = "From Alias",
      .description = "Converts an AliasTarget back to its AliasSource.",
      .pure_kind = NODE_PURE_CALL,
      .jit_kind = NODE_JIT_IDENTITY,
      .inputs = &target_pin,
      .input_count = 1,
      .outputs = &source_pin,
      .output_count = 1,
      .conversion_from = "AliasTarget",
      .conversion_to = "AliasSource",
      .fn = from_alias },
};
static const type_desc *const types[] = { &source_type, &target_type };
static node_desc *const nodes[] = { &conversions[0], &conversions[1] };
const registry_pack WHOLE_ALIAS_PACK = { .name = "Alias fixture",
                                         .types = types,
                                         .type_count = 2,
                                         .nodes = nodes,
                                         .node_count = 2 };

behavior_asset *whole_alias_fixture(const node_desc *conversion, bool edge, bool pure)
{
    behavior_asset *a = behavior_asset_new("Struct alias conversion");
    graph *g = behavior_asset_add_function(a, "Convert", pure);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    assert(conversion && conversion->jit_kind == NODE_JIT_IDENTITY);
    int pin = pure ? 0 : 1;
    assert(graph_add_pin(g, entry, 1, &conversion->inputs[0]) == pin);
    assert(graph_add_pin(g, ret, 0, &conversion->outputs[0]) == pin);
    if (edge) {
        assert(!graph_connect_data_conversion(g, entry, pin, ret, pin, conversion));
    } else {
        node *call = graph_add_node(g, conversion);
        assert(!graph_connect_data(g, entry, pin, call, 0));
        assert(!graph_connect_data(g, call, 0, ret, pin));
    }
    if (!pure) {
        assert(!graph_connect_exec(g, entry, 0, ret, 0, 0));
    }
    graph *outer = behavior_asset_add_function(a, "Nested conversion", false);
    entry = outer->nodes[0];
    ret = outer->nodes[1];
    assert(graph_add_pin(outer, entry, 1, &conversion->inputs[0]) == 1);
    assert(graph_add_pin(outer, ret, 0, &conversion->outputs[0]) == 1);
    node *call = behavior_asset_add_function_call_node_to_graph(a, outer, g);
    assert(!graph_connect_data(outer, entry, 1, call, pin));
    assert(!graph_connect_data(outer, call, pin, ret, 1));
    if (pure) {
        assert(!graph_connect_exec(outer, entry, 0, ret, 0, 0));
    } else {
        assert(!graph_connect_exec(outer, entry, 0, call, 0, 0));
        assert(!graph_connect_exec(outer, call, 0, ret, 0, 0));
    }
    return a;
}
void whole_alias_smoke()
{
    for (int reverse = 0; reverse < 2; reverse++) {
        behavior_asset *a = whole_alias_fixture(&conversions[reverse], true, true);
        behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        float vector[] = { -3.5f, 4.25f };
        pin_value input = { .type = PIN_STRUCT,
                            .ptr = vector,
                            .type_name = reverse ? "AliasTarget" : "AliasSource" };
        graph *g = a->graphs[2];
        flow_options options = { .instance = bi };
        flow_error error = { 0 };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &input, 1 }, &options, &error);
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        assert(run && !flow_run_resume(run) && run->whole_function_executed);
        const pin_value *out = flow_run_input_value(run, g->nodes[1], 1);
        assert(out->ptr != vector && !memcmp(out->ptr, vector, sizeof vector));
        assert(!strcmp(out->type_name, reverse ? "AliasSource" : "AliasTarget"));
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        flow_run_free(run);
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
    }
    test_note("PASS: whole struct alias conversions, owned results and no oracle calls\n");
}
