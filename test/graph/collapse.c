/* Collapse to Macro and Expand Node (Unreal): a macro's tunnels are the
   execution wires crossing the selection, and expanding a call puts the body
   it calls back in its place, wired as the call was. */
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/expand.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/extract.h"
#include "graph/nodes/core_nodes.h"
#include "support/array.h"
#include "test/graph/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    behavior_asset *asset;
    uint64_t result, event, branch, on_true, on_false;
} paths;

static paths p;
static behavior_asset *collapsed, *expanded;
static uint64_t *ids;

static void cleanup()
{
    afree(ids);
    ids = NULL;
    behavior_asset_free(expanded);
    behavior_asset_free(collapsed);
    behavior_asset_free(p.asset);
    p = (paths){ 0 };
    collapsed = expanded = NULL;
}

static node *set_result(behavior_asset *asset, uint64_t result, int value)
{
    node *set = behavior_asset_add_set_node(asset, result);
    pin_value v = { .type = PIN_I32, .i = value };
    int changed = graph_set_pin_default(asset->graph, set, 0, 1, &v);
    assert(changed == 0);
    (void)changed;
    return set;
}

static void exec_wire(graph *g, node *from, int from_pin, node *to)
{
    int connected = graph_connect_exec(g, from, from_pin, to, 0, 0);
    assert(connected == 0);
    (void)connected;
}

/* Test → Branch: True sets Result = 1, False sets Result = 2. */
static void branch_paths()
{
    p.asset = behavior_asset_new("Paths");
    graph *g = p.asset->graph;
    p.result = behavior_asset_add_variable(p.asset, "Result", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                   ->id;
    node *event = graph_add_node(g, &NODE_Event);
    graph_set_event_name(g, event, "Test");
    node *branch = graph_add_node(g, &NODE_Branch);
    node *on_true = set_result(p.asset, p.result, 1);
    node *on_false = set_result(p.asset, p.result, 2);
    exec_wire(g, event, 0, branch);
    exec_wire(g, branch, 0, on_true);
    exec_wire(g, branch, 1, on_false);
    p.event = event->id;
    p.branch = branch->id;
    p.on_true = on_true->id;
    p.on_false = on_false->id;
}

static int count_desc(const graph *g, const node_desc *desc)
{
    int count = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        count += g->nodes[i]->desc == desc;
    }
    return count;
}

static bool exec_linked(const graph *g, const node *from, int from_pin, uint64_t to)
{
    node *target = NULL;
    int pin = -1;
    return graph_exec_output_target(g, from, from_pin, 0, &target, &pin) &&
           target->id == to;
}

static node *driver(const graph *g, const node *n, int pin)
{
    node *source = NULL;
    int source_pin = -1;
    return graph_input_source(g, n, pin, &source, &source_pin) ? source : NULL;
}

static bool round_trips(const behavior_asset *asset)
{
    char *first = graph_asset_text(asset);
    behavior_asset *loaded = first ? graph_asset_read(first) : NULL;
    char *second = loaded ? graph_asset_text(loaded) : NULL;
    bool same = second && !strcmp(first, second);
    free(first);
    free(second);
    behavior_asset_free(loaded);
    return same;
}

/* A Branch's two exits become the macro's two tunnels, named as the Branch
   names them; expanding the call brings the Branch back. */
TEST_CASE(branches_collapse_to_macros_and_expand_back)
{
    branch_paths();
    graph *g = p.asset->graph;
    node *branch = graph_node_by_id(g, p.branch);
    REQUIRE(behavior_asset_can_extract_function(p.asset, g, &branch, 1) != NULL);
    REQUIRE(behavior_asset_can_extract_macro(p.asset, g, &branch, 1) == NULL);
    uint64_t call_id = 0;
    REQUIRE(!behavior_asset_extract_macro(p.asset, g, &branch, 1, "Pick", &collapsed, &call_id));
    REQUIRE(collapsed);
    graph *macro = behavior_asset_find_graph_by_name(collapsed, "Pick");
    node *call = graph_node_by_id(collapsed->graph, call_id);
    REQUIRE(macro && macro->kind == GRAPH_MACRO && call->desc == &NODE_CallMacro);
    REQUIRE(!graph_node_by_id(collapsed->graph, p.branch));
    REQUIRE(call->output_count == 2);
    REQUIRE(
        !strcmp(call->output_pins[0].name, "True") &&
        !strcmp(call->output_pins[1].name, "False")
    );
    REQUIRE(exec_linked(collapsed->graph, call, 0, p.on_true));
    REQUIRE(exec_linked(collapsed->graph, call, 1, p.on_false));
    REQUIRE(
        exec_linked(collapsed->graph, graph_node_by_id(collapsed->graph, p.event), 0, call_id)
    );
    REQUIRE(round_trips(collapsed));
    node *collapsed_branch = NULL;
    for (int i = 0; i < acount(macro->nodes); i++) {
        if (macro->nodes[i]->desc == &NODE_Branch) {
            collapsed_branch = macro->nodes[i];
        }
    }
    REQUIRE(collapsed_branch && collapsed_branch->input_pins[1].default_value.type == PIN_BOOL);
    REQUIRE(!collapsed_branch->input_pins[1].default_value.b);
    node *true_store = graph_node_by_id(collapsed->graph, p.on_true);
    node *false_store = graph_node_by_id(collapsed->graph, p.on_false);
    REQUIRE(true_store && false_store);
    REQUIRE(
        true_store->input_pins[1].default_value.type == PIN_I32 &&
        true_store->input_pins[1].default_value.i == 1
    );
    REQUIRE(
        false_store->input_pins[1].default_value.type == PIN_I32 &&
        false_store->input_pins[1].default_value.i == 2
    );

    REQUIRE(behavior_asset_can_expand_call(collapsed, collapsed->graph, call) == NULL);
    REQUIRE(!behavior_asset_expand_call(collapsed, collapsed->graph, call, &expanded, &ids));
    REQUIRE(expanded && acount(ids) == 1);
    graph *eg = expanded->graph;
    node *copy = graph_node_by_id(eg, ids[0]);
    REQUIRE(copy->desc == &NODE_Branch && !count_desc(eg, &NODE_CallMacro));
    REQUIRE(behavior_asset_find_graph_by_name(expanded, "Pick"));
    REQUIRE(exec_linked(eg, copy, 0, p.on_true) && exec_linked(eg, copy, 1, p.on_false));
    REQUIRE(exec_linked(eg, graph_node_by_id(eg, p.event), 0, copy->id));
    REQUIRE(round_trips(expanded));
    REQUIRE(
        copy->input_pins[1].default_value.type == PIN_BOOL &&
        !copy->input_pins[1].default_value.b
    );
    true_store = graph_node_by_id(eg, p.on_true);
    false_store = graph_node_by_id(eg, p.on_false);
    REQUIRE(true_store && false_store);
    REQUIRE(
        true_store->input_pins[1].default_value.type == PIN_I32 &&
        true_store->input_pins[1].default_value.i == 1
    );
    REQUIRE(
        false_store->input_pins[1].default_value.type == PIN_I32 &&
        false_store->input_pins[1].default_value.i == 2
    );
    return true;
}

static node *real(graph *g, double value)
{
    node *n = graph_add_node(g, graph_palette_node("Real"));
    pin_value v = { .type = PIN_F64, .type_name = "f64", .d = value };
    int set = graph_set_pin_default(g, n, 0, 0, &v);
    assert(set == 0);
    (void)set;
    return n;
}

static node *copied(const graph *g, const node_desc *desc)
{
    for (int i = 0; i < acount(ids); i++) {
        node *n = graph_node_by_id(g, ids[i]);
        if (n->desc == desc) {
            return n;
        }
    }
    return NULL;
}

/* A pure function's call expands into its calculations, fed by what fed the
   call and read by what read it: total = (a + b) * b + a. */
TEST_CASE(pure_calls_expand_into_their_calculations)
{
    p.asset = behavior_asset_new("Math");
    graph *g = p.asset->graph;
    node *a = real(g, 2.5), *b = real(g, 4);
    graph_set_node_presentation(g, a, NODE_PRESENTATION_EXPANDED);
    graph_set_node_presentation(g, b, NODE_PRESENTATION_EXPANDED);
    node *sum = graph_add_node(g, &NODE_Add),
         *product = graph_add_node(g, &NODE_Multiply);
    node *total = graph_add_node(g, &NODE_Add);
    REQUIRE(graph_connect(g, a, 0, sum, 0) == 0 && graph_connect(g, b, 0, sum, 1) == 0);
    REQUIRE(
        graph_connect(g, sum, 0, product, 0) == 0 &&
        graph_connect(g, b, 0, product, 1) == 0
    );
    REQUIRE(
        graph_connect(g, product, 0, total, 0) == 0 &&
        graph_connect(g, a, 0, total, 1) == 0
    );
    node *selection[] = { sum, product };
    uint64_t call_id = 0;
    REQUIRE(
        !behavior_asset_extract_function(p.asset, g, selection, 2, "Scale", &collapsed, &call_id)
    );
    node *call = graph_node_by_id(collapsed->graph, call_id);
    REQUIRE(call && call->desc == &NODE_CallPureFunction);
    REQUIRE(!behavior_asset_expand_call(collapsed, collapsed->graph, call, &expanded, &ids));
    graph *eg = expanded->graph;
    REQUIRE(acount(ids) == 2 && !count_desc(eg, &NODE_CallPureFunction));
    node *ea = graph_node_by_id(eg, a->id), *eb = graph_node_by_id(eg, b->id);
    node *etotal = graph_node_by_id(eg, total->id);
    node *esum = copied(eg, &NODE_Add), *eproduct = copied(eg, &NODE_Multiply);
    REQUIRE(esum && eproduct && ea && eb);
    REQUIRE(
        ea->input_pins[0].default_value.type == PIN_F64 &&
        ea->input_pins[0].default_value.d == 2.5
    );
    REQUIRE(
        eb->input_pins[0].default_value.type == PIN_F64 &&
        eb->input_pins[0].default_value.d == 4
    );
    REQUIRE(driver(eg, esum, 0) == ea && driver(eg, esum, 1) == eb);
    REQUIRE(driver(eg, eproduct, 0) == esum && driver(eg, eproduct, 1) == eb);
    REQUIRE(driver(eg, etotal, 0) == eproduct && driver(eg, etotal, 1) == ea);
    return true;
}

/* Execution into a function call enters its body, and what followed the call
   follows the body. */
TEST_CASE(executable_calls_expand_into_their_body)
{
    branch_paths();
    graph *g = p.asset->graph;
    node *event = graph_node_by_id(g, p.event);
    node *store = set_result(p.asset, p.result, 5);
    node *print = graph_add_node(g, &NODE_PrintString);
    graph_remove_node(g, graph_node_by_id(g, p.branch));
    exec_wire(g, event, 0, store);
    exec_wire(g, store, 0, print);
    uint64_t call_id = 0;
    REQUIRE(
        !behavior_asset_extract_function(p.asset, g, &store, 1, "Store", &collapsed, &call_id)
    );
    node *call = graph_node_by_id(collapsed->graph, call_id);
    REQUIRE(!behavior_asset_expand_call(collapsed, collapsed->graph, call, &expanded, &ids));
    REQUIRE(acount(ids) == 1);
    graph *eg = expanded->graph;
    node *body = graph_node_by_id(eg, ids[0]);
    REQUIRE(body->desc == &NODE_SetVariable && body->reference_id == p.result);
    REQUIRE(
        body->input_pins[1].default_value.type == PIN_I32 &&
        body->input_pins[1].default_value.i == 5
    );
    REQUIRE(exec_linked(eg, graph_node_by_id(eg, p.event), 0, ids[0]));
    REQUIRE(exec_linked(eg, body, 0, print->id));
    return true;
}

/* A function call continues wherever its body ends, so a body with an
   unwired execution output keeps its call; so do calls into the graph
   itself. */
TEST_CASE(calls_that_cannot_expand_keep_their_call)
{
    p.asset = behavior_asset_new("Refusals");
    behavior_asset *asset = p.asset;
    graph *function = behavior_asset_add_function(asset, "Open", false);
    uint64_t result = behavior_asset_add_variable(asset, "Result", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                          ->id;
    node *set = behavior_asset_add_set_node_to_graph(asset, function, result);
    exec_wire(function, graph_node_by_id(function, function->entry_id), 0, set);
    node *call = behavior_asset_add_function_call_node_to_graph(asset, asset->graph, function);
    REQUIRE(behavior_asset_can_expand_call(asset, asset->graph, call) != NULL);
    exec_wire(function, set, 0, graph_node_by_id(function, function->return_id));
    REQUIRE(behavior_asset_can_expand_call(asset, asset->graph, call) == NULL);
    node *inner = behavior_asset_add_function_call_node_to_graph(asset, function, function);
    REQUIRE(behavior_asset_can_expand_call(asset, function, inner) != NULL);
    /* A macro needs execution entering the selection. */
    node *print = graph_add_node(asset->graph, &NODE_PrintString);
    REQUIRE(behavior_asset_can_extract_macro(asset, asset->graph, &print, 1) != NULL);
    return true;
}

void test_graph_collapse()
{
    graph_palette_open();
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(branches_collapse_to_macros_and_expand_back);
    RUN_TEST_CASE(pure_calls_expand_into_their_calculations);
    RUN_TEST_CASE(executable_calls_expand_into_their_body);
    RUN_TEST_CASE(calls_that_cannot_expand_keep_their_call);
    pu_clear_setup();
    graph_palette_close();
}
