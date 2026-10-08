#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/nodes/core_nodes.h"
/* Collapse to Macro and Expand Node (Unreal): a macro's tunnels are the
   execution wires crossing the selection, and expanding a call puts the body
   it calls back in its place. Both keep what the behavior does. */
#include "graph/authoring/expand.h"
#include "graph/document/extract.h"
#include "graph/document/library.h"
#include "test/suite_internal.h"

typedef struct
{
    behavior_asset *asset;
    uint64_t result, event, branch, on_true, on_false;
} paths;

static node *set_result(behavior_asset *asset, uint64_t result, int value)
{
    node *set = behavior_asset_add_set_node(asset, result);
    assert(set);
    int changed = graph_set_pin_default(asset->graph, set, 0, 1, &(pin_value){ .type = PIN_I32, .i = value });
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

/* Test → Branch(false) → True: Result = 1, False: Result = 2. */
static paths branch_paths()
{
    paths p = { .asset = behavior_asset_new("Paths") };
    graph *g = p.asset->graph;
    p.result = behavior_asset_add_variable(p.asset, "Result", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                   ->id;
    node *event = graph_add_node(g, &NODE_Event);
    graph_set_event_name(g, event, "Test");
    node *branch = graph_add_node(g, &NODE_Branch);
    node *on_true = set_result(p.asset, p.result, 1),
         *on_false = set_result(p.asset, p.result, 2);
    exec_wire(g, event, 0, branch);
    exec_wire(g, branch, 0, on_true);
    exec_wire(g, branch, 1, on_false);
    p.event = event->id;
    p.branch = branch->id;
    p.on_true = on_true->id;
    p.on_false = on_false->id;
    return p;
}

/* Runs Test and reads Result. */
static int run_result(const behavior_asset *asset, uint64_t result)
{
    char error[256];
    behavior_asset *linked = behavior_asset_link_libraries(asset, NULL, 0, error, sizeof error);
    assert(linked);
    behavior_instance *instance = behavior_instance_create(linked, NULL, NULL, NULL);
    flow_error failure = { 0 };
    behavior_instance_dispatch(instance, "Test", (flow_inputs){ 0 }, &failure);
    pin_value value = { 0 };
    behavior_instance_get_variable(instance, result, &value);
    behavior_instance_destroy(instance);
    behavior_asset_free(linked);
    return value.i;
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

static bool round_trips(const behavior_asset *asset)
{
    char *first = behavior_asset_save(asset);
    behavior_asset *loaded = behavior_asset_load(first);
    char *second = loaded ? behavior_asset_save(loaded) : NULL;
    bool same = second && !strcmp(first, second);
    if (!same) {
        static int failure;
        char path[128];
        const char *texts[] = { first, second };
        for (int i = 0; i < 2; i++) {
            snprintf(path, sizeof path, "build/requirements-collapse-%d-%s.chibi", failure, i ? "actual" : "expected");
            FILE *file = fopen(path, "w");
            if (file) {
                fputs(texts[i] ? texts[i] : "<load failed>", file);
                fclose(file);
            }
        }
        failure++;
    }
    free(first);
    free(second);
    behavior_asset_free(loaded);
    return same;
}

/* A Branch's two exits become the macro's two tunnels, named as the Branch
   names them; expanding the call brings the Branch back. */
static void branch_macro()
{
    paths p = branch_paths();
    graph *g = p.asset->graph;
    node *branch = graph_node_by_id(g, p.branch);
    test_check(run_result(p.asset, p.result) == 2, "collapse: the false branch runs before collapsing");
    test_check(behavior_asset_can_extract_function(p.asset, g, &branch, 1) != NULL, "collapse: a branch cannot become a function");
    test_check(behavior_asset_can_extract_macro(p.asset, g, &branch, 1) == NULL, "collapse: a branch can become a macro");
    behavior_asset *collapsed = NULL;
    uint64_t call_id = 0;
    const char *error = behavior_asset_extract_macro(p.asset, g, &branch, 1, "Pick", &collapsed, &call_id);
    test_check(!error && collapsed, "collapse: the branch collapses to a macro");
    if (error) {
        behavior_asset_free(p.asset);
        return;
    }
    graph *macro = behavior_asset_find_graph_by_name(collapsed, "Pick");
    node *call = graph_node_by_id(collapsed->graph, call_id);
    test_check(
        macro && macro->kind == GRAPH_MACRO && call->desc == &NODE_CallMacro &&
            !graph_node_by_id(collapsed->graph, p.branch),
        "collapse: a macro call replaces the selection"
    );
    test_check(
        call->output_count == 2 && !strcmp(call->output_pins[0].name, "True") &&
            !strcmp(call->output_pins[1].name, "False"),
        "collapse: each exit is a tunnel named after the pin it leaves by"
    );
    test_check(
        exec_linked(collapsed->graph, call, 0, p.on_true) &&
            exec_linked(collapsed->graph, call, 1, p.on_false) &&
            exec_linked(collapsed->graph, graph_node_by_id(collapsed->graph, p.event), 0, call_id),
        "collapse: the call takes the selection's execution wires"
    );
    test_check(run_result(collapsed, p.result) == 2, "collapse: the macro runs the same path");
    test_check(round_trips(collapsed), "collapse: the collapsed behavior saves and loads unchanged");

    test_check(behavior_asset_can_expand_call(collapsed, collapsed->graph, call) == NULL, "expand: a macro call can expand");
    behavior_asset *expanded = NULL;
    uint64_t *ids = NULL;
    error = behavior_asset_expand_call(collapsed, collapsed->graph, call, &expanded, &ids);
    test_check(!error && expanded && acount(ids) == 1, "expand: the macro's body replaces its call");
    if (!error) {
        graph *eg = expanded->graph;
        node *copy = graph_node_by_id(eg, ids[0]);
        test_check(
            copy->desc == &NODE_Branch && !count_desc(eg, &NODE_CallMacro) &&
                behavior_asset_find_graph_by_name(expanded, "Pick") &&
                exec_linked(eg, copy, 0, p.on_true) &&
                exec_linked(eg, copy, 1, p.on_false) &&
                exec_linked(eg, graph_node_by_id(eg, p.event), 0, copy->id),
            "expand: the body takes the call's wires and the macro stays"
        );
        test_check(run_result(expanded, p.result) == 2, "expand: the expanded body runs the same path");
        test_check(round_trips(expanded), "expand: the expanded behavior saves and loads unchanged");
    }
    afree(ids);
    behavior_asset_free(expanded);
    behavior_asset_free(collapsed);
    behavior_asset_free(p.asset);
}

static node *real(graph *g, double value)
{
    node *n = graph_add_node(g, registry_node_by_name("Real"));
    int set = graph_set_pin_default(
        g,
        n,
        0,
        0,
        &(pin_value){ .type = PIN_F64, .type_name = "double", .d = value }
    );
    assert(n && set == 0);
    (void)set;
    return n;
}

static double evaluate(behavior_asset *asset, uint64_t id)
{
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    eval_context context;
    eval_context_init(&context);
    context.instance = instance;
    arena memory;
    arena_init(&memory, 128);
    pin_value value = { 0 };
    int evaluated = graph_eval_output(
        asset->graph,
        graph_node_by_id(asset->graph, id),
        0,
        &memory,
        &value,
        &context
    );
    double result = evaluated == 0 ? value.d : NAN;
    eval_context_free(&context);
    arena_free(&memory);
    behavior_instance_destroy(instance);
    return result;
}

/* A pure function's call expands into its calculations, fed by what fed the
   call and read by what read it. */
static void pure_function()
{
    behavior_asset *asset = behavior_asset_new("Math");
    graph *g = asset->graph;
    node *a = real(g, 2.5), *b = real(g, 4);
    graph_set_node_presentation(g, a, NODE_PRESENTATION_EXPANDED);
    graph_set_node_presentation(g, b, NODE_PRESENTATION_EXPANDED);
    node *sum = graph_add_node(g, &NODE_Add),
         *product = graph_add_node(g, &NODE_Multiply);
    node *total = graph_add_node(g, &NODE_Add);
    int wired = graph_connect(g, a, 0, sum, 0) | graph_connect(g, b, 0, sum, 1) |
                graph_connect(g, sum, 0, product, 0) |
                graph_connect(g, b, 0, product, 1) |
                graph_connect(g, product, 0, total, 0) |
                graph_connect(g, a, 0, total, 1);
    assert(wired == 0);
    (void)wired;
    uint64_t total_id = total->id;
    double before = evaluate(asset, total_id);
    node *selection[] = { sum, product };
    behavior_asset *collapsed = NULL, *expanded = NULL;
    uint64_t call_id = 0, *ids = NULL;
    const char *error = behavior_asset_extract_function(asset, g, selection, 2, "Scale", &collapsed, &call_id);
    assert(!error);
    node *call = graph_node_by_id(collapsed->graph, call_id);
    error = behavior_asset_expand_call(collapsed, collapsed->graph, call, &expanded, &ids);
    test_check(
        !error && acount(ids) == 2 && !count_desc(expanded->graph, &NODE_CallPureFunction),
        "expand: a pure call expands into its calculations"
    );
    test_check(
        !error && fabs(evaluate(expanded, total_id) - before) < 1e-9 &&
            before == (2.5 + 4) * 4 + 2.5,
        "expand: the expanded calculations give the same result"
    );
    afree(ids);
    behavior_asset_free(expanded);
    behavior_asset_free(collapsed);
    behavior_asset_free(asset);
}

/* Execution into a function call enters its body, and what followed the call
   follows the body. */
static void exec_function()
{
    paths p = branch_paths();
    graph *g = p.asset->graph;
    node *event = graph_node_by_id(g, p.event),
         *store = set_result(p.asset, p.result, 5);
    node *print = graph_add_node(g, &NODE_PrintString);
    graph_remove_node(g, graph_node_by_id(g, p.branch));
    exec_wire(g, event, 0, store);
    exec_wire(g, store, 0, print);
    behavior_asset *collapsed = NULL, *expanded = NULL;
    uint64_t call_id = 0, *ids = NULL;
    const char *error = behavior_asset_extract_function(p.asset, g, &store, 1, "Store", &collapsed, &call_id);
    assert(!error);
    node *call = graph_node_by_id(collapsed->graph, call_id);
    error = behavior_asset_expand_call(collapsed, collapsed->graph, call, &expanded, &ids);
    test_check(
        !error && acount(ids) == 1 &&
            exec_linked(expanded->graph, graph_node_by_id(expanded->graph, p.event), 0, ids[0]) &&
            exec_linked(
                expanded->graph,
                graph_node_by_id(expanded->graph, ids[0]),
                0,
                print->id
            ) &&
            run_result(expanded, p.result) == 5,
        "expand: a function call's execution runs through its body"
    );
    afree(ids);
    behavior_asset_free(expanded);
    behavior_asset_free(collapsed);
    behavior_asset_free(p.asset);
}

/* A function call continues wherever its body ends, so a body with an
   unwired execution output keeps its call; so do calls into the graph
   itself. */
static void refusals()
{
    behavior_asset *asset = behavior_asset_new("Refusals");
    graph *function = behavior_asset_add_function(asset, "Open", false);
    uint64_t result = behavior_asset_add_variable(asset, "Result", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                          ->id;
    node *set = behavior_asset_add_set_node_to_graph(asset, function, result);
    exec_wire(function, graph_node_by_id(function, function->entry_id), 0, set);
    node *call = behavior_asset_add_function_call_node_to_graph(asset, asset->graph, function);
    test_check(
        behavior_asset_can_expand_call(asset, asset->graph, call) != NULL,
        "expand: a function that can end before Return keeps its call"
    );
    exec_wire(function, set, 0, graph_node_by_id(function, function->return_id));
    test_check(
        behavior_asset_can_expand_call(asset, asset->graph, call) == NULL,
        "expand: a function whose paths reach Return expands"
    );
    node *inner = behavior_asset_add_function_call_node_to_graph(asset, function, function);
    test_check(behavior_asset_can_expand_call(asset, function, inner) != NULL, "expand: a call into its own graph keeps its call");
    node *print = graph_add_node(asset->graph, &NODE_PrintString);
    test_check(
        behavior_asset_can_extract_macro(asset, asset->graph, &print, 1) != NULL,
        "collapse: a macro needs execution entering the selection"
    );
    behavior_asset_free(asset);
}

void run_graph_collapse_checks()
{
    branch_macro();
    pure_function();
    exec_function();
    refusals();
}
