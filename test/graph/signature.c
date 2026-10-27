#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/graph_signature.h"
#include "graph/nodes/core_nodes.h"
#include "support/array.h"
#include "test/graph/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

static const pin_spec EXEC_PIN = { .kind = PIN_EXEC, .type = PIN_FLOW };

static behavior_asset *asset;

static void cleanup()
{
    behavior_asset_free(asset);
    asset = NULL;
}

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

static bool wired_from(const graph *g, const node *n, int pin, const node *from, int from_pin)
{
    node *source = NULL;
    int source_pin = -1;
    return graph_input_source(g, n, pin, &source, &source_pin) &&
           source == from && source_pin == from_pin;
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

static bool runs_into(const graph *g, const node *from, const node *to, int pin)
{
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind == GRAPH_EDGE_EXEC && e->from_id == from->id &&
            e->to_id == to->id && e->to_pin_id == to->input_pins[pin].id) {
            return true;
        }
    }
    return false;
}

static bool named(const pin_spec *pins, int count, const char *const *expected)
{
    for (int i = 0; i < count; i++) {
        if (!pins[i].name || strcmp(pins[i].name, expected[i])) {
            return false;
        }
    }
    return true;
}

/* A float variable's Get, to wire into calls. */
static node *float_get(const char *name)
{
    behavior_variable *v = behavior_asset_add_variable(asset, name, PIN_F32, "number", PIN_CONTAINER_SINGLE, NULL, NULL);
    return behavior_asset_add_get_node(asset, v->id);
}

static bool saves_and_loads()
{
    char *document = graph_asset_text(asset);
    behavior_asset *loaded = document ? graph_asset_read(document) : NULL;
    bool ok = loaded != NULL;
    behavior_asset_free(loaded);
    free(document);
    return ok;
}

TEST_CASE(function_signature_edits_reach_every_call)
{
    asset = behavior_asset_new("Signature");
    graph *fn = behavior_asset_add_function(asset, "Scale", 0);
    node *entry = graph_node_by_id(fn, fn->entry_id);
    node *ret = graph_node_by_id(fn, fn->return_id);
    graph_connect_exec(fn, entry, 0, ret, 0, 0);
    node *call = behavior_asset_add_function_call_node(asset, fn);
    pin_spec value = { .name = "Value", .type = PIN_F32, .type_name = "number" };
    REQUIRE(graph_signature_add(asset, fn, false, &value) == 0);
    REQUIRE(find_pin(entry, 1, "Value") == 1 && find_pin(call, 0, "Value") == 1);
    REQUIRE(call->input_pins[1].declaration_id == entry->output_pins[1].id);
    REQUIRE(graph_signature_add(asset, fn, false, &value) != 0);
    graph_connect_data(asset->graph, float_get("Input"), 0, call, 1);
    REQUIRE(graph_signature_rename(asset, fn, false, 1, "Amount") == 0);
    REQUIRE(find_pin(call, 0, "Amount") == 1 && wired(asset->graph, call, 1));
    pin_spec result = { .name = "Scaled", .type = PIN_F32, .type_name = "number" };
    REQUIRE(graph_signature_add(asset, fn, true, &result) == 0);
    REQUIRE(find_pin(ret, 0, "Scaled") == 1 && find_pin(call, 1, "Scaled") == 1);
    /* Retyping drops call wires the new type can't carry. */
    pin_spec vector = { .type = PIN_STRUCT, .type_name = "vec2" };
    REQUIRE(graph_signature_retype(asset, fn, false, 1, &vector) == 0);
    REQUIRE(entry->output_pins[1].type == PIN_STRUCT && call->input_pins[1].type == PIN_STRUCT);
    REQUIRE(!wired(asset->graph, call, 1));
    REQUIRE(graph_signature_remove(asset, fn, false, 1) == 0);
    REQUIRE(find_pin(entry, 1, "Amount") < 0 && find_pin(call, 0, "Amount") < 0);
    REQUIRE(find_pin(call, 1, "Scaled") == 1);
    REQUIRE(saves_and_loads());
    return true;
}

TEST_CASE(purity_toggles_execution_pins_on_calls)
{
    asset = behavior_asset_new("Purity");
    graph *fn = behavior_asset_add_function(asset, "Double", 0);
    node *entry = graph_node_by_id(fn, fn->entry_id);
    node *ret = graph_node_by_id(fn, fn->return_id);
    graph_connect_exec(fn, entry, 0, ret, 0, 0);
    pin_spec value = { .name = "Value", .type = PIN_F32, .type_name = "number" };
    graph_signature_add(asset, fn, false, &value);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, event, "Run");
    node *call = behavior_asset_add_function_call_node(asset, fn);
    node *print = graph_add_node(asset->graph, &NODE_PrintString);
    graph_connect_exec(asset->graph, event, 0, call, 0, 0);
    graph_connect_exec(asset->graph, call, 0, print, 0, 0);
    graph_connect_data(asset->graph, float_get("Input"), 0, call, find_pin(call, 0, "Value"));
    /* A body that runs in order can't be made pure. */
    node *body = graph_add_node(fn, &NODE_PrintString);
    REQUIRE(!graph_signature_pure(fn) && graph_signature_set_pure(asset, fn, true) != 0);
    graph_remove_node(fn, body);
    REQUIRE(graph_signature_set_pure(asset, fn, true) == 0 && graph_signature_pure(fn));
    REQUIRE(call->desc == &NODE_CallPureFunction && find_pin(call, 0, "Exec") < 0);
    REQUIRE(wired(asset->graph, call, 0));
    /* A call made pure hands its execution to the node that followed it. */
    REQUIRE(exec_edges(asset->graph, event->id, print->id) == 1);
    REQUIRE(graph_signature_set_pure(asset, fn, false) == 0 && !graph_signature_pure(fn));
    REQUIRE(call->desc == &NODE_CallFunction && find_pin(call, 0, "Exec") == 0);
    REQUIRE(find_pin(call, 1, "Then") == 0 && find_pin(call, 0, "Value") == 1);
    REQUIRE(exec_edges(fn, entry->id, ret->id) == 1);
    return true;
}

/* Shader and field graphs compute values only. */
TEST_CASE(material_functions_stay_pure)
{
    asset = behavior_asset_new("Tint");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *shade = behavior_asset_add_function(asset, "Shade", 1);
    REQUIRE(shade && graph_signature_pure(shade));
    REQUIRE(graph_signature_set_pure(asset, shade, false) != 0);
    return true;
}

TEST_CASE(variables_duplicate_and_own_their_defaults)
{
    asset = behavior_asset_new("Members");
    pin_value four = { .type = PIN_F32, .f = 4 };
    behavior_variable *speed = behavior_asset_add_variable(
        asset,
        "Speed",
        PIN_F32,
        "number",
        PIN_CONTAINER_SINGLE,
        &four,
        "Motion"
    );
    uint64_t id = speed->id;
    behavior_variable *copy = behavior_asset_duplicate_variable(asset, id, "Speed 2");
    REQUIRE(copy && copy->type == PIN_F32 && copy->default_value.f == 4);
    REQUIRE(copy->category && !strcmp(copy->category, "Motion") && copy->id != id);
    behavior_variable *aim = behavior_asset_add_variable(
        asset,
        "Aim",
        PIN_STRUCT,
        "vec2",
        PIN_CONTAINER_SINGLE,
        NULL,
        NULL
    );
    uint64_t aim_id = aim->id;
    float xy[2] = { 3, 4 };
    pin_value vector = { .type = PIN_STRUCT, .ptr = xy, .type_name = "vec2" };
    REQUIRE(behavior_asset_set_variable_default(asset, aim_id, &vector) == 0);
    xy[0] = 0;
    aim = behavior_asset_find_variable_by_id(asset, aim_id);
    REQUIRE(aim->default_value.ptr != xy && ((float *)aim->default_value.ptr)[0] == 3);
    /* Get and Set nodes go in any graph; deleting the uses removes both. */
    graph *fn = behavior_asset_add_function(asset, "Step", 0);
    node *get = behavior_asset_add_get_node_to_graph(asset, fn, id);
    node *set = behavior_asset_add_set_node_to_graph(asset, asset->graph, id);
    REQUIRE(get && set && graph_node_by_id(fn, get->id) == get);
    REQUIRE(get->reference_id == id && set->input_pins[1].type == PIN_F32);
    uint64_t get_id = get->id;
    REQUIRE(
        behavior_asset_remove_variable_uses(asset, id) == 2 &&
        !graph_node_by_id(fn, get_id)
    );
    return true;
}

TEST_CASE(macro_signature_edits_reach_every_call)
{
    asset = behavior_asset_new("Macro signature");
    graph *macro = behavior_asset_add_macro(asset, "Gate");
    node *entry = graph_node_by_id(macro, macro->entry_id);
    node *call = behavior_asset_add_macro_call_node(asset, macro);
    pin_spec alternate = EXEC_PIN;
    alternate.name = "Alternate";
    REQUIRE(graph_signature_add(asset, macro, false, &alternate) == 0);
    int tunnel = find_pin(entry, 1, "Alternate");
    REQUIRE(tunnel >= 0 && entry->output_pins[tunnel].kind == PIN_EXEC);
    REQUIRE(
        call->input_pins[find_pin(call, 0, "Alternate")].declaration_id ==
        entry->output_pins[tunnel].id
    );
    /* A function's inputs are data only. */
    graph *fn = behavior_asset_add_function(asset, "Plain", 0);
    REQUIRE(graph_signature_add(asset, fn, false, &alternate) != 0);
    pin_spec amount = { .name = "Amount",
                        .kind = PIN_DATA,
                        .type = PIN_F32,
                        .type_name = "number" };
    REQUIRE(graph_signature_add(asset, macro, false, &amount) == 0);
    graph_connect_data(asset->graph, float_get("Input"), 0, call, find_pin(call, 0, "Amount"));
    int at = find_pin(entry, 1, "Amount");
    REQUIRE(graph_signature_rename(asset, macro, false, at, "Count") == 0);
    REQUIRE(
        find_pin(call, 0, "Count") >= 0 &&
        wired(asset->graph, call, find_pin(call, 0, "Count"))
    );
    /* Retyped to Exec, the input becomes an execution pin and drops its
       data wire; it then takes a data type and container again. */
    REQUIRE(graph_signature_retype(asset, macro, false, at, &EXEC_PIN) == 0);
    REQUIRE(call->input_pins[find_pin(call, 0, "Count")].kind == PIN_EXEC);
    REQUIRE(acount(asset->graph->edges) == 0);
    pin_spec list = { .kind = PIN_DATA,
                      .type = PIN_I32,
                      .type_name = "int",
                      .container = PIN_CONTAINER_ARRAY };
    REQUIRE(graph_signature_retype(asset, macro, false, at, &list) == 0);
    REQUIRE(call->input_pins[find_pin(call, 0, "Count")].container == PIN_CONTAINER_ARRAY);
    REQUIRE(entry->output_pins[at].kind == PIN_DATA);
    REQUIRE(graph_signature_remove(asset, macro, false, find_pin(entry, 1, "Alternate")) == 0);
    REQUIRE(find_pin(entry, 1, "Alternate") < 0 && find_pin(call, 0, "Alternate") < 0);
    char *document = graph_asset_text(asset);
    behavior_asset *loaded = document ? graph_asset_read(document) : NULL;
    graph *loaded_macro = loaded ? behavior_asset_find_graph(loaded, macro->graph_id)
                                 : NULL;
    node *loaded_call = loaded ? graph_node_by_id(loaded->graph, call->id) : NULL;
    node *loaded_entry = loaded_macro
                           ? graph_node_by_id(loaded_macro, loaded_macro->entry_id)
                           : NULL;
    bool reloaded = loaded_entry && loaded_call &&
                    loaded_entry->output_count == loaded_call->input_count;
    for (int i = 0; reloaded && i < loaded_call->input_count; i++) {
        const pin_spec *tunnel = &loaded_entry->output_pins[i];
        const pin_spec *input = &loaded_call->input_pins[i];
        reloaded = tunnel->name && input->name &&
                   !strcmp(tunnel->name, input->name) &&
                   tunnel->kind == input->kind && tunnel->type == input->type &&
                   tunnel->container == input->container &&
                   tunnel->id == input->declaration_id;
    }
    behavior_asset_free(loaded);
    free(document);
    REQUIRE(reloaded);
    return true;
}

TEST_CASE(input_defaults_reach_calls_that_kept_them)
{
    asset = behavior_asset_new("Defaults");
    graph *fn = behavior_asset_add_function(asset, "Move", 1);
    node *entry = graph_node_by_id(fn, fn->entry_id);
    pin_spec speed = { .name = "Speed", .kind = PIN_DATA, .type = PIN_F32, .type_name = "number" };
    graph_signature_add(asset, fn, false, &speed);
    node *kept = behavior_asset_add_function_call_node(asset, fn);
    node *edited = behavior_asset_add_function_call_node(asset, fn);
    int at = find_pin(entry, 1, "Speed");
    pin_value nine = { .type = PIN_F32, .f = 9 }, five = { .type = PIN_F32, .f = 5 };
    REQUIRE(
        graph_set_pin_default(asset->graph, edited, 0, find_pin(edited, 0, "Speed"), &nine) == 0
    );
    REQUIRE(graph_signature_set_default(asset, fn, at, &five) == 0);
    REQUIRE(entry->output_pins[at].default_value.f == 5);
    REQUIRE(kept->input_pins[find_pin(kept, 0, "Speed")].default_value.f == 5);
    REQUIRE(edited->input_pins[find_pin(edited, 0, "Speed")].default_value.f == 9);
    node *later = behavior_asset_add_function_call_node(asset, fn);
    REQUIRE(later->input_pins[find_pin(later, 0, "Speed")].default_value.f == 5);
    const pin_spec *declared = graph_signature_call_input(asset, edited, find_pin(edited, 0, "Speed"));
    REQUIRE(declared && declared->default_value.f == 5);
    REQUIRE(!graph_signature_call_input(asset, entry, 0));
    pin_value flag = { .type = PIN_BOOL, .b = true };
    REQUIRE(graph_signature_set_default(asset, fn, at, &flag) != 0);
    return true;
}

/* Promote to Variable on an output runs the new Set after the node when the
   node's Then is free, as Unreal wires it, and leaves it unwired otherwise. */
TEST_CASE(promoted_outputs_run_after_a_free_then)
{
    asset = behavior_asset_new("Promote");
    graph *fn = behavior_asset_add_function(asset, "Roll", 0);
    pin_spec roll = { .name = "Roll", .kind = PIN_DATA, .type = PIN_F32, .type_name = "number" };
    graph_signature_add(asset, fn, true, &roll);
    node *free_call = behavior_asset_add_function_call_node(asset, fn);
    node *busy_call = behavior_asset_add_function_call_node(asset, fn);
    node *next = graph_add_node(asset->graph, &NODE_PrintString);
    graph_connect_exec(asset->graph, busy_call, 0, next, 0, 0);
    REQUIRE(
        behavior_asset_promote_pin_to_variable(asset, free_call, 1, find_pin(free_call, 1, "Roll"), "Rolled")
    );
    node *set = alast(asset->graph->nodes);
    REQUIRE(
        set->desc == &NODE_SetVariable &&
        exec_edges(asset->graph, free_call->id, set->id) == 1
    );
    REQUIRE(
        behavior_asset_promote_pin_to_variable(asset, busy_call, 1, find_pin(busy_call, 1, "Roll"), "Busy")
    );
    set = alast(asset->graph->nodes);
    REQUIRE(exec_edges(asset->graph, busy_call->id, set->id) == 0);
    REQUIRE(exec_edges(asset->graph, busy_call->id, next->id) == 1);
    return true;
}

/* A second Return as MCP's add_node makes one, with the first's data pins. */
static node *add_return(graph *fn)
{
    node *first = graph_node_by_id(fn, fn->return_id);
    node *second = graph_add_node(fn, &NODE_Return);
    for (int i = 0; i < first->input_count; i++) {
        if (first->input_pins[i].kind == PIN_DATA) {
            graph_add_pin(fn, second, 0, &first->input_pins[i]);
        }
    }
    return second;
}

TEST_CASE(moved_function_pins_move_on_returns_and_calls)
{
    asset = behavior_asset_new("Moves");
    graph *fn = behavior_asset_add_function(asset, "Mix", 0);
    node *entry = graph_node_by_id(fn, fn->entry_id);
    node *ret = graph_node_by_id(fn, fn->return_id);
    graph_connect_exec(fn, entry, 0, ret, 0, 0);
    const char *const signature[] = { "A", "B", "Low", "High" };
    for (int i = 0; i < 4; i++) {
        pin_spec pin = { .name = signature[i], .type = PIN_F32, .type_name = "number" };
        REQUIRE(graph_signature_add(asset, fn, i >= 2, &pin) == 0);
    }
    node *second = add_return(fn);
    node *call = behavior_asset_add_function_call_node(asset, fn);
    node *a = float_get("First");
    graph_connect_data(asset->graph, a, 0, call, find_pin(call, 0, "A"));
    pin_value three = { .type = PIN_F32, .f = 3 };
    graph_set_pin_default(asset->graph, call, 0, find_pin(call, 0, "B"), &three);
    uint64_t kept = behavior_asset_add_variable(asset, "Kept", PIN_F32, "number", PIN_CONTAINER_SINGLE, NULL, NULL)
                        ->id;
    node *set = behavior_asset_add_set_node(asset, kept);
    graph_connect_data(asset->graph, call, find_pin(call, 1, "High"), set, 1);
    /* A pin moves only onto another pin its side declares, and a refused
       move changes nothing. */
    char *before = graph_asset_text(asset);
    bool refused = graph_signature_move(asset, fn, false, 1, 0) != 0 &&
                   graph_signature_move(asset, fn, false, 1, 3) != 0 &&
                   graph_signature_move(asset, fn, true, 0, 1) != 0;
    char *after = graph_asset_text(asset);
    bool unchanged = before && after && !strcmp(before, after);
    free(before);
    free(after);
    REQUIRE(refused && unchanged);
    REQUIRE(graph_signature_move(asset, fn, false, 2, 1) == 0);
    REQUIRE(named(entry->output_pins, 3, (const char *[]){ "Exec", "B", "A" }));
    REQUIRE(named(call->input_pins, 3, (const char *[]){ "Exec", "B", "A" }));
    REQUIRE(
        wired_from(asset->graph, call, 2, a, 0) &&
        call->input_pins[1].default_value.f == 3
    );
    REQUIRE(graph_signature_move(asset, fn, true, 2, 1) == 0);
    REQUIRE(named(ret->input_pins, 3, (const char *[]){ "Exec", "High", "Low" }));
    REQUIRE(named(second->input_pins, 3, (const char *[]){ "Exec", "High", "Low" }));
    REQUIRE(named(call->output_pins, 3, (const char *[]){ "Then", "High", "Low" }));
    REQUIRE(wired_from(asset->graph, set, 1, call, 1));
    return true;
}

/* A macro's calls mirror its tunnels, execution pins included. */
TEST_CASE(moved_macro_pins_move_on_calls)
{
    asset = behavior_asset_new("Macro moves");
    graph *macro = behavior_asset_add_macro(asset, "Route");
    node *entry = graph_node_by_id(macro, macro->entry_id);
    pin_spec amount = { .name = "Amount",
                        .kind = PIN_DATA,
                        .type = PIN_F32,
                        .type_name = "number" };
    pin_spec side = EXEC_PIN;
    side.name = "Side";
    REQUIRE(graph_signature_add(asset, macro, false, &amount) == 0);
    REQUIRE(graph_signature_add(asset, macro, false, &side) == 0);
    node *call = behavior_asset_add_macro_call_node(asset, macro);
    node *number = float_get("Number");
    node *event = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, event, "Go");
    graph_connect_data(asset->graph, number, 0, call, find_pin(call, 0, "Amount"));
    graph_connect_exec(asset->graph, event, 0, call, find_pin(call, 0, "Side"), 0);
    REQUIRE(graph_signature_move(asset, macro, false, 2, 0) == 0);
    REQUIRE(named(entry->output_pins, 3, (const char *[]){ "Side", "Exec", "Amount" }));
    REQUIRE(named(call->input_pins, 3, (const char *[]){ "Side", "Exec", "Amount" }));
    REQUIRE(
        wired_from(asset->graph, call, 2, number, 0) &&
        runs_into(asset->graph, event, call, 0)
    );
    return true;
}

void test_graph_signature()
{
    graph_palette_open();
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(function_signature_edits_reach_every_call);
    RUN_TEST_CASE(purity_toggles_execution_pins_on_calls);
    RUN_TEST_CASE(material_functions_stay_pure);
    RUN_TEST_CASE(variables_duplicate_and_own_their_defaults);
    RUN_TEST_CASE(macro_signature_edits_reach_every_call);
    RUN_TEST_CASE(input_defaults_reach_calls_that_kept_them);
    RUN_TEST_CASE(promoted_outputs_run_after_a_free_then);
    RUN_TEST_CASE(moved_function_pins_move_on_returns_and_calls);
    RUN_TEST_CASE(moved_macro_pins_move_on_calls);
    pu_clear_setup();
    graph_palette_close();
}
