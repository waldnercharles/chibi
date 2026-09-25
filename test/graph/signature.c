#include "graph/document/function_signature.h"
#include "test/suite_internal.h"

static int find_pin(const node *n, int output, const char *name)
{
    int count = output ? n->output_count : n->input_count;
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    for (int i = 0; i < count; i++) {
        if (pins[i].name && !strcmp(pins[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static bool wired(const graph *g, const node *n, int pin)
{
    node *source = NULL;
    int source_pin = -1;
    return graph_input_source(g, n, pin, &source, &source_pin);
}

/* A float to wire into calls. */
static node *float_source(behavior_asset *asset)
{
    behavior_variable *v = behavior_asset_add_variable(
        asset,
        "Input",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        NULL,
        NULL
    );
    return behavior_asset_add_get_node(asset, v->id);
}

static int exec_edges(const graph *g, uint64_t from, uint64_t to)
{
    int count = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        count += e->kind == GRAPH_EDGE_EXEC && e->from_id == from && e->to_id == to;
    }
    return count;
}

static void check_signature_edits()
{
    behavior_asset *asset = behavior_asset_new("Signature");
    graph *fn = behavior_asset_add_function(asset, "Scale", 0);
    node *entry = graph_node_by_id(fn, fn->entry_id);
    node *ret = graph_node_by_id(fn, fn->return_id);
    graph_connect_exec(fn, entry, 0, ret, 0, 0);
    node *call = behavior_asset_add_function_call_node(asset, fn);
    pin_spec value = { .name = "Value", .type = PIN_F32, .type_name = "float" };
    test_check(
        function_signature_add(asset, fn, false, &value) == 0 &&
            find_pin(entry, 1, "Value") == 1 && find_pin(call, 0, "Value") == 1 &&
            call->input_pins[1].declaration_id == entry->output_pins[1].id,
        "a new parameter appears on Function Entry and on every call"
    );
    test_check(function_signature_add(asset, fn, false, &value) != 0, "parameter names stay unique");
    node *number = float_source(asset);
    graph_connect_data(asset->graph, number, 0, call, 1);
    test_check(
        function_signature_rename(asset, fn, false, 1, "Amount") == 0 &&
            find_pin(call, 0, "Amount") == 1 && wired(asset->graph, call, 1),
        "renaming a parameter renames its call pins and keeps their wires"
    );
    pin_spec result = { .name = "Scaled", .type = PIN_F32, .type_name = "float" };
    test_check(
        function_signature_add(asset, fn, true, &result) == 0 &&
            find_pin(ret, 0, "Scaled") == 1 && find_pin(call, 1, "Scaled") == 1,
        "a new result appears on Return and as a call output"
    );
    pin_spec vector = { .type = PIN_STRUCT, .type_name = "CF_V2" };
    test_check(
        function_signature_retype(asset, fn, false, 1, &vector) == 0 &&
            entry->output_pins[1].type == PIN_STRUCT &&
            call->input_pins[1].type == PIN_STRUCT && !wired(asset->graph, call, 1),
        "retyping a parameter retypes its call pins and drops wires they can't "
        "carry"
    );
    test_check(
        function_signature_remove(asset, fn, false, 1) == 0 &&
            find_pin(entry, 1, "Amount") < 0 &&
            find_pin(call, 0, "Amount") < 0 && find_pin(call, 1, "Scaled") == 1,
        "removing a parameter removes it from every call"
    );
    char *json = behavior_asset_save_json(asset);
    behavior_asset *loaded = json ? behavior_asset_load_json(json) : NULL;
    test_check(loaded != NULL, "an edited signature saves and loads");
    behavior_asset_free(loaded);
    free(json);
    behavior_asset_free(asset);
}

static void check_purity()
{
    behavior_asset *asset = behavior_asset_new("Purity");
    graph *fn = behavior_asset_add_function(asset, "Double", 0);
    node *entry = graph_node_by_id(fn, fn->entry_id);
    node *ret = graph_node_by_id(fn, fn->return_id);
    graph_connect_exec(fn, entry, 0, ret, 0, 0);
    pin_spec value = { .name = "Value", .type = PIN_F32, .type_name = "float" };
    function_signature_add(asset, fn, false, &value);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, event, "Run");
    node *call = behavior_asset_add_function_call_node(asset, fn);
    node *print = graph_add_node(asset->graph, &NODE_PrintString);
    node *number = float_source(asset);
    graph_connect_exec(asset->graph, event, 0, call, 0, 0);
    graph_connect_exec(asset->graph, call, 0, print, 0, 0);
    graph_connect_data(asset->graph, number, 0, call, find_pin(call, 0, "Value"));
    node *body = graph_add_node(fn, &NODE_PrintString);
    test_check(
        !function_signature_pure(fn) &&
            function_signature_set_pure(asset, fn, true) != 0,
        "a function whose body runs in order can't be made pure"
    );
    graph_remove_node(fn, body);
    test_check(
        function_signature_set_pure(asset, fn, true) == 0 &&
            function_signature_pure(fn) && call->desc == &NODE_CallPureFunction &&
            find_pin(call, 0, "Exec") < 0 && wired(asset->graph, call, 0),
        "a pure function's calls lose their execution pins and keep their data "
        "wires"
    );
    test_check(
        exec_edges(asset->graph, event->id, print->id) == 1,
        "a call made pure hands its execution to the node that followed it"
    );
    test_check(
        function_signature_set_pure(asset, fn, false) == 0 &&
            !function_signature_pure(fn) && call->desc == &NODE_CallFunction &&
            find_pin(call, 0, "Exec") == 0 && find_pin(call, 1, "Then") == 0 &&
            find_pin(call, 0, "Value") == 1 && exec_edges(fn, entry->id, ret->id) == 1,
        "an impure function's calls regain execution pins and its entry runs "
        "to Return"
    );
    compile_report report;
    compile_report_init(&report);
    test_check(behavior_asset_compile(asset, &report) == 0, "a function toggled pure and back compiles");
    compile_report_free(&report);
    behavior_asset_free(asset);
}

static void check_variable_members()
{
    behavior_asset *asset = behavior_asset_new("Members");
    behavior_variable *speed = behavior_asset_add_variable(
        asset,
        "Speed",
        PIN_F32,
        "float",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_F32, .f = 4 },
        "Motion"
    );
    uint64_t id = speed->id;
    behavior_variable *copy = behavior_asset_duplicate_variable(asset, id, "Speed 2");
    test_check(
        copy && copy->type == PIN_F32 && copy->default_value.f == 4 &&
            copy->category && !strcmp(copy->category, "Motion") && copy->id != id,
        "a duplicated variable keeps its type, default and category"
    );
    behavior_variable *aim = behavior_asset_add_variable(
        asset,
        "Aim",
        PIN_STRUCT,
        "CF_V2",
        PIN_CONTAINER_SINGLE,
        NULL,
        NULL
    );
    float xy[2] = { 3, 4 };
    uint64_t aim_id = aim->id;
    int set_aim = behavior_asset_set_variable_default(
        asset,
        aim_id,
        &(pin_value){ .type = PIN_STRUCT, .ptr = xy, .type_name = "CF_V2" }
    );
    xy[0] = 0;
    aim = behavior_asset_find_variable_by_id(asset, aim_id);
    test_check(
        set_aim == 0 && aim->default_value.ptr != xy &&
            ((float *)aim->default_value.ptr)[0] == 3,
        "a variable keeps its own copy of a vector default"
    );
    graph *fn = behavior_asset_add_function(asset, "Step", 0);
    node *get = behavior_asset_add_get_node_to_graph(asset, fn, id);
    node *set = behavior_asset_add_set_node_to_graph(asset, asset->graph, id);
    test_check(
        get && set && graph_node_by_id(fn, get->id) == get &&
            get->reference_id == id && set->input_pins[1].type == PIN_F32,
        "Get and Set nodes can be added to any graph of the behavior"
    );
    uint64_t get_id = get->id;
    test_check(
        behavior_asset_remove_variable_uses(asset, id) == 2 &&
            !graph_node_by_id(fn, get_id),
        "deleting a variable's uses removes its Get and Set nodes"
    );
    behavior_asset_free(asset);
}

void run_graph_signature_checks()
{
    check_signature_edits();
    check_purity();
    check_variable_members();
}
