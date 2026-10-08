#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/behavior_program.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/runtime.h"
#include "support/arena.h"
#include "support/array.h"
#include "test/whole_aggregate_fixture.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static pin_spec pair_pins[] = { { .name = "x", .type = PIN_F32, .type_name = "float" },
                                { .name = "y", .type = PIN_F32, .type_name = "float" } };
static pin_spec record_pins[] = {
    { .name = "tag", .type = PIN_U8, .type_name = "byte" },
    { .name = "position", .type = PIN_STRUCT, .type_name = "WholePair" },
    { .name = "weight", .type = PIN_F64, .type_name = "double" },
    { .name = "active", .type = PIN_BOOL, .type_name = "bool" },
    { .name = "stamp", .type = PIN_I64, .type_name = "int64" }
};
static pin_spec pair_pin[] = {
    { .name = "Value", .type = PIN_STRUCT, .type_name = "WholePair" }
};
static pin_spec record_pin[] = {
    { .name = "Value", .type = PIN_STRUCT, .type_name = "WholeRecord" }
};
static node_desc make_pair = { .name = "Make WholePair",
                               .pure_kind = NODE_PURE_MAKE,
                               .type = &WHOLE_PAIR_TYPE,
                               .inputs = pair_pins,
                               .input_count = 2,
                               .outputs = pair_pin,
                               .output_count = 1 };
static node_desc make_record = { .name = "Make WholeRecord",
                                 .pure_kind = NODE_PURE_MAKE,
                                 .type = &WHOLE_RECORD_TYPE,
                                 .inputs = record_pins,
                                 .input_count = 5,
                                 .outputs = record_pin,
                                 .output_count = 1 };
static node_desc break_record = { .name = "Break WholeRecord",
                                  .pure_kind = NODE_PURE_BREAK,
                                  .type = &WHOLE_RECORD_TYPE,
                                  .inputs = record_pin,
                                  .input_count = 1,
                                  .outputs = record_pins,
                                  .output_count = 5 };
static const type_desc *const types[] = { &WHOLE_PAIR_TYPE, &WHOLE_RECORD_TYPE };
static node_desc *const nodes[] = { &make_pair,
                                    &make_record,
                                    &break_record,
                                    &WHOLE_NODE_GetRecord,
                                    &WHOLE_NODE_SetWeight };
const registry_pack WHOLE_VALUE_PACK = { .name = "Whole values",
                                         .types = types,
                                         .type_count = 2,
                                         .nodes = nodes,
                                         .node_count = 5 };

behavior_asset *whole_aggregate_fixture()
{
    behavior_asset *a = behavior_asset_new("Aggregate bodies");
    graph *f = behavior_asset_add_function(a, "Move", 1);
    node *entry = graph_node_by_id(f, f->entry_id),
         *ret = graph_node_by_id(f, f->return_id);
    pin_spec delta = { .name = "Delta", .type = PIN_F32, .type_name = "float" };
    assert(graph_add_pin(f, entry, 1, record_pin) == 0);
    assert(graph_add_pin(f, entry, 1, &delta) == 1);
    assert(graph_add_pin(f, ret, 0, record_pin) == 0);
    node *fields = graph_add_node(f, &break_record),
         *pair = graph_add_node(f, &make_pair),
         *record = graph_add_node(f, &make_record);
    assert(graph_connect_data(f, entry, 0, fields, 0) == 0);
    assert(graph_expose_output_fields(f, fields, 1) == 0);
    for (int i = 0; i < 2; i++) {
        node *op = graph_add_node(f, i ? &NODE_Subtract : &NODE_Add);
        assert(graph_connect_data(f, fields, 5 + i, op, 0) == 0);
        assert(graph_connect_data(f, entry, 1, op, 1) == 0);
        assert(graph_connect_data(f, op, 0, pair, i) == 0);
    }
    for (int i = 0; i < 5; i++) {
        assert(graph_connect_data(f, i == 1 ? pair : fields, i == 1 ? 0 : i, record, i) == 0);
    }
    assert(graph_connect_data(f, record, 0, ret, 0) == 0);
    graph *wrapper = behavior_asset_add_function(a, "Move twice", 1);
    node *we = graph_node_by_id(wrapper, wrapper->entry_id),
         *wr = graph_node_by_id(wrapper, wrapper->return_id);
    assert(graph_add_pin(wrapper, we, 1, record_pin) == 0);
    assert(graph_add_pin(wrapper, we, 1, &delta) == 1);
    assert(graph_add_pin(wrapper, wr, 0, record_pin) == 0);
    assert(graph_add_pin(wrapper, wr, 0, &delta) == 1);
    node *first = behavior_asset_add_function_call_node_to_graph(a, wrapper, f);
    node *second = behavior_asset_add_function_call_node_to_graph(a, wrapper, f);
    assert(graph_connect_data(wrapper, we, 0, first, 0) == 0);
    assert(graph_connect_data(wrapper, first, 0, second, 0) == 0);
    assert(graph_connect_data(wrapper, we, 1, first, 1) == 0);
    assert(graph_connect_data(wrapper, we, 1, second, 1) == 0);
    assert(graph_connect_data(wrapper, second, 0, wr, 0) == 0);
    assert(graph_connect_data(wrapper, we, 1, wr, 1) == 0);
    node *call = behavior_asset_add_function_call_node(a, wrapper);
    for (int i = 0; i < 2; i++) {
        pin_spec *p = &we->output_pins[i];
        uint64_t id = behavior_asset_add_variable(a, p->name, p->type, p->type_name, PIN_CONTAINER_SINGLE, NULL, "")
                          ->id;
        node *get = behavior_asset_add_get_node(a, id);
        assert(graph_connect_data(a->graph, get, 0, call, i) == 0);
    }
    assert(graph_expose_output_fields(a->graph, call, 0) == 0);
    return a;
}
behavior_asset *whole_aggregate_scopes()
{
    behavior_asset *a = whole_aggregate_fixture();
    graph *target = a->graphs[2];
    for (int depth = 0; depth < 2; depth++) {
        graph *g = behavior_asset_add_function(a, depth ? "Outer" : "Inner", 0);
        node *entry = g->nodes[0], *ret = g->nodes[1];
        pin_spec delta = { .name = "Delta", .type = PIN_F32, .type_name = "float" };
        assert(graph_add_pin(g, entry, 1, record_pin) == 1);
        assert(graph_add_pin(g, entry, 1, &delta) == 2);
        assert(graph_add_pin(g, ret, 0, record_pin) == 1);
        assert(graph_add_pin(g, ret, 0, &delta) == 2);
        node *call = behavior_asset_add_function_call_node_to_graph(a, g, target);
        for (int i = 0; i < 2; i++) {
            assert(!graph_connect_data(g, entry, i + 1, call, i + depth));
            assert(!graph_connect_data(g, call, i + depth, ret, i + 1));
        }
        if (depth) {
            assert(!graph_connect_exec(g, entry, 0, call, 0, 0));
            assert(!graph_connect_exec(g, call, 0, ret, 0, 0));
        } else {
            assert(!graph_connect_exec(g, entry, 0, ret, 0, 0));
        }
        target = g;
    }
    return a;
}
behavior_asset *whole_aggregate_identity(bool project)
{
    behavior_asset *a = behavior_asset_new("Aggregate identity");
    graph *g = behavior_asset_add_function(a, "Identity", 1);
    node *entry = graph_node_by_id(g, g->entry_id),
         *ret = graph_node_by_id(g, g->return_id);
    assert(graph_add_pin(g, entry, 1, record_pin) == 0);
    if (project) {
        node *fields = graph_add_node(g, &break_record);
        assert(graph_connect_data(g, entry, 0, fields, 0) == 0);
        assert(graph_add_pin(g, ret, 0, &record_pins[2]) == 0);
        assert(graph_connect_data(g, fields, 2, ret, 0) == 0);
    } else {
        assert(graph_add_pin(g, ret, 0, record_pin) == 0);
        assert(graph_connect_data(g, entry, 0, ret, 0) == 0);
    }
    node *call = behavior_asset_add_function_call_node(a, g);
    uint64_t id = behavior_asset_add_variable(a, "Record", PIN_STRUCT, "WholeRecord", PIN_CONTAINER_SINGLE, NULL, "")
                      ->id;
    node *get = behavior_asset_add_get_node(a, id);
    assert(graph_connect_data(a->graph, get, 0, call, 0) == 0);
    return a;
}
void whole_aggregate_smoke()
{
    behavior_asset *a = whole_aggregate_fixture();
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    behavior_instance_set_trace_enabled(bi, 0);
    graph *g = a->graphs[2];
    assert(behavior_program_function(
        behavior_program_graph(behavior_program_cached(a), g),
        g->nodes[0]
    ));
    whole_record input = { .tag = 251,
                           .position = { 3, 4 },
                           .weight = 2.75,
                           .active = true,
                           .stamp = INT64_MIN + 7 };
    pin_value v = { .type = PIN_STRUCT, .type_name = "WholeRecord", .ptr = &input };
    assert(!behavior_instance_set_variable(bi, a->variables[0].id, &v));
    bi->execution.variables[1].f = .25f;
    arena storage = { 0 };
    eval_context ctx = { .instance = bi, .runtime = runtime_store_create(a->graph) };
    uint64_t calls = eval_oracle_calls(), allocations = runtime_store_creations();
    assert(!behavior_program_output(
        behavior_program_graph(behavior_program_cached(a), a->graph),
        a->graph->nodes[0],
        0,
        &storage,
        &v,
        &ctx,
        behavior_instance_backend(bi)
    ));
    const whole_record *r = v.ptr;
    assert(r && r != &input && r->position.x == 3.5f && r->position.y == 3.5f);
    assert(
        r->tag == input.tag && r->weight == input.weight && r->active &&
        r->stamp == input.stamp
    );
    assert(calls == eval_oracle_calls() && allocations == runtime_store_creations());
    runtime_store_free(ctx.runtime);
    arena_free(&storage);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
    puts("PASS: whole nested aggregates, projected fields, scalar ABI and owned results; no callee allocation or oracle calls");
}
