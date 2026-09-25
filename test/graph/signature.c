#include "graph/document/graph_signature.h"
#include "graph/document/library.h"
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
        graph_signature_add(asset, fn, false, &value) == 0 &&
            find_pin(entry, 1, "Value") == 1 && find_pin(call, 0, "Value") == 1 &&
            call->input_pins[1].declaration_id == entry->output_pins[1].id,
        "a new parameter appears on Function Entry and on every call"
    );
    test_check(graph_signature_add(asset, fn, false, &value) != 0, "parameter names stay unique");
    node *number = float_source(asset);
    graph_connect_data(asset->graph, number, 0, call, 1);
    test_check(
        graph_signature_rename(asset, fn, false, 1, "Amount") == 0 &&
            find_pin(call, 0, "Amount") == 1 && wired(asset->graph, call, 1),
        "renaming a parameter renames its call pins and keeps their wires"
    );
    pin_spec result = { .name = "Scaled", .type = PIN_F32, .type_name = "float" };
    test_check(
        graph_signature_add(asset, fn, true, &result) == 0 &&
            find_pin(ret, 0, "Scaled") == 1 && find_pin(call, 1, "Scaled") == 1,
        "a new result appears on Return and as a call output"
    );
    pin_spec vector = { .type = PIN_STRUCT, .type_name = "CF_V2" };
    test_check(
        graph_signature_retype(asset, fn, false, 1, &vector) == 0 &&
            entry->output_pins[1].type == PIN_STRUCT &&
            call->input_pins[1].type == PIN_STRUCT && !wired(asset->graph, call, 1),
        "retyping a parameter retypes its call pins and drops wires they can't "
        "carry"
    );
    test_check(
        graph_signature_remove(asset, fn, false, 1) == 0 &&
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
    graph_signature_add(asset, fn, false, &value);
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
        !graph_signature_pure(fn) && graph_signature_set_pure(asset, fn, true) != 0,
        "a function whose body runs in order can't be made pure"
    );
    graph_remove_node(fn, body);
    test_check(
        graph_signature_set_pure(asset, fn, true) == 0 &&
            graph_signature_pure(fn) && call->desc == &NODE_CallPureFunction &&
            find_pin(call, 0, "Exec") < 0 && wired(asset->graph, call, 0),
        "a pure function's calls lose their execution pins and keep their data "
        "wires"
    );
    test_check(
        exec_edges(asset->graph, event->id, print->id) == 1,
        "a call made pure hands its execution to the node that followed it"
    );
    test_check(
        graph_signature_set_pure(asset, fn, false) == 0 && !graph_signature_pure(fn) &&
            call->desc == &NODE_CallFunction && find_pin(call, 0, "Exec") == 0 &&
            find_pin(call, 1, "Then") == 0 && find_pin(call, 0, "Value") == 1 &&
            exec_edges(fn, entry->id, ret->id) == 1,
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

static bool rule_reported(const compile_report *report, int rule)
{
    for (int i = 0; i < acount(report->items); i++) {
        if (report->items[i].rule == rule) {
            return true;
        }
    }
    return false;
}

static const pin_spec EXEC_PIN = { .kind = PIN_EXEC, .type = PIN_FLOW };

static void check_macro_signature()
{
    behavior_asset *asset = behavior_asset_new("Macro signature");
    graph *macro = behavior_asset_add_macro(asset, "Gate");
    node *entry = graph_node_by_id(macro, macro->entry_id);
    node *call = behavior_asset_add_macro_call_node(asset, macro);
    pin_spec alternate = EXEC_PIN;
    alternate.name = "Alternate";
    test_check(
        graph_signature_add(asset, macro, false, &alternate) == 0 &&
            entry->output_pins[find_pin(entry, 1, "Alternate")].kind == PIN_EXEC &&
            call->input_pins[find_pin(call, 0, "Alternate")].declaration_id ==
                entry->output_pins[find_pin(entry, 1, "Alternate")].id,
        "a macro's new execution input appears on its tunnel and every call"
    );
    graph *fn = behavior_asset_add_function(asset, "Plain", 0);
    test_check(graph_signature_add(asset, fn, false, &alternate) != 0, "a function's inputs are data only");
    pin_spec amount = { .name = "Amount",
                        .kind = PIN_DATA,
                        .type = PIN_F32,
                        .type_name = "float" };
    graph_signature_add(asset, macro, false, &amount);
    node *number = float_source(asset);
    graph_connect_data(asset->graph, number, 0, call, find_pin(call, 0, "Amount"));
    int at = find_pin(entry, 1, "Amount");
    test_check(
        graph_signature_rename(asset, macro, false, at, "Count") == 0 &&
            find_pin(call, 0, "Count") >= 0 &&
            wired(asset->graph, call, find_pin(call, 0, "Count")),
        "renaming a macro input renames its call pins and keeps their wires"
    );
    test_check(
        graph_signature_retype(asset, macro, false, at, &EXEC_PIN) == 0 &&
            call->input_pins[find_pin(call, 0, "Count")].kind == PIN_EXEC &&
            acount(asset->graph->edges) == 0,
        "a macro input retyped to Exec becomes an execution pin and drops its "
        "data "
        "wire"
    );
    pin_spec list = { .kind = PIN_DATA,
                      .type = PIN_I32,
                      .type_name = "int",
                      .container = PIN_CONTAINER_ARRAY };
    test_check(
        graph_signature_retype(asset, macro, false, at, &list) == 0 &&
            call->input_pins[find_pin(call, 0, "Count")].container == PIN_CONTAINER_ARRAY &&
            entry->output_pins[at].kind == PIN_DATA,
        "a macro input takes a data type and a container again"
    );
    test_check(
        graph_signature_remove(asset, macro, false, find_pin(entry, 1, "Alternate")) == 0 &&
            find_pin(entry, 1, "Alternate") < 0 && find_pin(call, 0, "Alternate") < 0,
        "removing a macro input removes it from every call"
    );
    char *json = behavior_asset_save_json(asset);
    behavior_asset *loaded = json ? behavior_asset_load_json(json) : NULL;
    const graph *reloaded = loaded ? behavior_asset_find_graph(loaded, macro->graph_id)
                                   : NULL;
    compile_report report;
    compile_report_init(&report);
    if (loaded) {
        behavior_asset_compile(loaded, &report);
    }
    test_check(
        reloaded && !rule_reported(&report, CMPL_RULE_CALL_SIGNATURE),
        "an edited macro saves, loads and its calls still match it"
    );
    compile_report_free(&report);
    behavior_asset_free(loaded);
    free(json);
    behavior_asset_free(asset);
}

/* A macro's added execution pins pair with its calls' pins in order. */
static void check_macro_paths()
{
    behavior_asset *asset = behavior_asset_new("Macro paths");
    uint64_t result = behavior_asset_add_variable(asset, "Result", PIN_I32, "int", PIN_CONTAINER_SINGLE, NULL, "")
                          ->id;
    graph *macro = behavior_asset_add_macro(asset, "Route");
    node *entry = graph_node_by_id(macro, macro->entry_id);
    node *ret = graph_node_by_id(macro, macro->return_id);
    pin_spec side = EXEC_PIN, late = EXEC_PIN;
    side.name = "Side";
    late.name = "Late";
    node *call = behavior_asset_add_macro_call_node(asset, macro);
    graph_signature_add(asset, macro, false, &side);
    graph_signature_add(asset, macro, true, &late);
    graph_connect_exec(macro, entry, 0, ret, 0, 0);
    graph_connect_exec(macro, entry, find_pin(entry, 1, "Side"), ret, find_pin(ret, 0, "Late"), 0);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, event, "Test");
    node *set = behavior_asset_add_set_node(asset, result);
    graph_set_pin_default(asset->graph, set, 0, 1, &(pin_value){ .type = PIN_I32, .i = 7 });
    graph_connect_exec(asset->graph, event, 0, call, find_pin(call, 0, "Side"), 0);
    graph_connect_exec(asset->graph, call, find_pin(call, 1, "Late"), set, 0, 0);
    char error[256];
    behavior_asset *linked = behavior_asset_link_libraries(asset, NULL, 0, error, sizeof error);
    behavior_instance *bi = linked ? behavior_instance_create(linked, NULL, NULL, NULL)
                                   : NULL;
    flow_error flow_error = { 0 };
    if (bi) {
        behavior_instance_dispatch(bi, "Test", (flow_inputs){ 0 }, &flow_error);
    }
    pin_value value = { 0 };
    if (bi) {
        behavior_instance_get_variable(bi, result, &value);
    }
    test_check(value.i == 7, "a macro's added execution input runs to its added exit");
    behavior_instance_destroy(bi);
    behavior_asset_free(linked);
    behavior_asset_free(asset);
}

static void check_input_defaults()
{
    behavior_asset *asset = behavior_asset_new("Defaults");
    graph *fn = behavior_asset_add_function(asset, "Move", 1);
    node *entry = graph_node_by_id(fn, fn->entry_id);
    pin_spec speed = { .name = "Speed", .kind = PIN_DATA, .type = PIN_F32, .type_name = "float" };
    graph_signature_add(asset, fn, false, &speed);
    node *kept = behavior_asset_add_function_call_node(asset, fn);
    node *edited = behavior_asset_add_function_call_node(asset, fn);
    int at = find_pin(entry, 1, "Speed");
    graph_set_pin_default(
        asset->graph,
        edited,
        0,
        find_pin(edited, 0, "Speed"),
        &(pin_value){ .type = PIN_F32, .f = 9 }
    );
    test_check(
        graph_signature_set_default(asset, fn, at, &(pin_value){ .type = PIN_F32, .f = 5 }) ==
                0 &&
            entry->output_pins[at].default_value.f == 5 &&
            kept->input_pins[find_pin(kept, 0, "Speed")].default_value.f == 5 &&
            edited->input_pins[find_pin(edited, 0, "Speed")].default_value.f == 9,
        "an input's new default reaches calls that kept the old one, not "
        "values "
        "set on a call"
    );
    node *later = behavior_asset_add_function_call_node(asset, fn);
    test_check(later->input_pins[find_pin(later, 0, "Speed")].default_value.f == 5, "a new call takes its inputs' defaults");
    test_check(
        graph_signature_set_default(asset, fn, at, &(pin_value){ .type = PIN_BOOL, .b = true }) !=
            0,
        "a default takes its input's type"
    );
    behavior_asset_free(asset);
}

/* Shader and field graphs compute values only. */
static void check_pure_domains()
{
    behavior_asset *asset = behavior_asset_new("Tint");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    editor *e = editor_new(asset);
    editor_command create = { .kind = EDITOR_COMMAND_CREATE_GRAPH,
                              .data.create_graph = { GRAPH_FUNCTION, "Shade" } };
    graph *made = editor_command_apply(e, create) == 0
                    ? behavior_asset_find_graph_by_name(asset, "Shade")
                    : NULL;
    test_check(made && graph_signature_pure(made), "a new material function is pure");
    test_check(made && graph_signature_set_pure(asset, made, false) != 0, "a material function can't be made impure");
    editor_free(e);
}

/* Promote to Variable on an output runs the new Set after the node when the
   node's Then is free, as Unreal wires it, and leaves it unwired otherwise. */
static void check_promote_flow()
{
    behavior_asset *asset = behavior_asset_new("Promote");
    graph *fn = behavior_asset_add_function(asset, "Roll", 0);
    pin_spec roll = { .name = "Roll", .kind = PIN_DATA, .type = PIN_F32, .type_name = "float" };
    graph_signature_add(asset, fn, true, &roll);
    node *free_call = behavior_asset_add_function_call_node(asset, fn);
    node *busy_call = behavior_asset_add_function_call_node(asset, fn);
    node *next = graph_add_node(asset->graph, &NODE_PrintString);
    graph_connect_exec(asset->graph, busy_call, 0, next, 0, 0);
    behavior_asset_promote_pin_to_variable(asset, free_call, 1, find_pin(free_call, 1, "Roll"), "Rolled");
    node *set = alast(asset->graph->nodes);
    test_check(
        set->desc == &NODE_SetVariable &&
            exec_edges(asset->graph, free_call->id, set->id) == 1,
        "a promoted output's Set runs after the node's free Then"
    );
    behavior_asset_promote_pin_to_variable(asset, busy_call, 1, find_pin(busy_call, 1, "Roll"), "Busy");
    set = alast(asset->graph->nodes);
    test_check(
        exec_edges(asset->graph, busy_call->id, set->id) == 0 &&
            exec_edges(asset->graph, busy_call->id, next->id) == 1,
        "a node whose Then is wired keeps its flow, and its Set is left unwired"
    );
    behavior_asset_free(asset);
}

void run_graph_signature_checks()
{
    check_signature_edits();
    check_purity();
    check_variable_members();
    check_macro_signature();
    check_macro_paths();
    check_input_defaults();
    check_pure_domains();
    check_promote_flow();
}
