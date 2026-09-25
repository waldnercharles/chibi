/* Canvas editing rules shared by Studio and automation: variadic pins, pin
   links and defaults, promotion, and node alignment. */
#include "graph/authoring/align.h"
#include "graph/authoring/pin_edit.h"
#include "graph/nodes/state_nodes.h"
#include "test/suite_internal.h"

#include <math.h>

static node *add(graph *g, const char *type)
{
    node *n = graph_add_node(g, registry_node_by_name(type));
    assert(n);
    return n;
}

static bool names(const node *n, int output, const char *const *expected, int count)
{
    int have = output ? n->output_count : n->input_count;
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    bool same = have == count;
    for (int i = 0; same && i < count; i++) {
        same = pins[i].name && !strcmp(pins[i].name, expected[i]);
    }
    return same;
}

static bool connected_to(const graph *g, const node *n, int output, int pin)
{
    uint64_t id = (output ? n->output_pins : n->input_pins)[pin].id;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (output ? e->from_id == n->id && e->from_pin_id == id
                   : e->to_id == n->id && e->to_pin_id == id) {
            return true;
        }
    }
    return false;
}

static void sequence_runs()
{
    graph g = { 0 };
    graph_init(&g);
    node *sequence = graph_add_node(&g, &NODE_Sequence);
    node *print = add(&g, "Print String");
    graph_pin_run run;
    test_check(
        graph_pin_run_of(sequence, &run) && run.output && run.first == 0 &&
            run.count == 2,
        "Sequence grows its outputs"
    );
    test_check(
        graph_add_run_pin(&g, sequence) == 2 &&
            names(sequence, 1, (const char *[]){ "Then0", "Then1", "Then2" }, 3) &&
            sequence->output_pins[2].kind == PIN_EXEC,
        "a new Sequence output continues the numbering"
    );
    test_check(graph_connect_exec(&g, sequence, 2, print, 0, 0) == 0, "the new output takes a wire");
    test_check(
        graph_remove_run_pin(&g, sequence, 1, 0) == 0 &&
            names(sequence, 1, (const char *[]){ "Then0", "Then1" }, 2) &&
            connected_to(&g, sequence, 1, 1),
        "removing an output renumbers the rest and keeps their wires"
    );
    test_check(
        graph_run_pin_removable(sequence, 1, 1) &&
            !graph_run_pin_removable(sequence, 0, 0) &&
            graph_remove_run_pin(&g, sequence, 1, 1) == 0 &&
            !graph_run_pin_removable(sequence, 1, 0) && acount(g.edges) == 0,
        "a run keeps its last pin, and a removed pin takes its wires"
    );
    node *branch = graph_add_node(&g, &NODE_Branch);
    test_check(!graph_pin_run_of(branch, &run) && graph_add_run_pin(&g, branch) < 0, "fixed nodes have no run");
    graph_free(&g);
}

static void switch_runs()
{
    graph g = { 0 };
    graph_init(&g);
    node *cases = graph_add_node(&g, &NODE_SwitchInt);
    test_check(
        graph_add_run_pin(&g, cases) == 2 &&
            names(cases, 1, (const char *[]){ "Case0", "Case1", "Case2", "Default" }, 4),
        "a switch's new case goes before Default"
    );
    test_check(
        graph_run_pin_removable(cases, 1, 2) && !graph_run_pin_removable(cases, 1, 3),
        "a switch's Default stays"
    );
    char *json = graph_save_json(&g);
    graph *copy = json ? graph_load_json(json) : NULL;
    node *loaded = copy ? graph_node_by_id(copy, cases->id) : NULL;
    test_check(
        loaded &&
            names(loaded, 1, (const char *[]){ "Case0", "Case1", "Case2", "Default" }, 4),
        "an added case survives saving with Default last"
    );
    free(json);
    if (copy) {
        graph_free(copy);
        free(copy);
    }
    node *text = graph_add_node(&g, &NODE_SwitchString);
    graph_set_pin_name(&g, text, 1, 1, "Case:Fire");
    test_check(
        graph_remove_run_pin(&g, text, 1, 0) == 0 &&
            names(text, 1, (const char *[]){ "Case:Fire", "Default" }, 2) &&
            graph_add_run_pin(&g, text) == 1 &&
            names(text, 1, (const char *[]){ "Case:Fire", "Case1", "Default" }, 3),
        "authored case names keep their text"
    );
    graph_free(&g);
}

static void wildcard_runs()
{
    graph g = { 0 };
    graph_init(&g);
    node *select = graph_add_node(&g, &NODE_Select);
    node *value = add(&g, "Integer");
    test_check(graph_connect(&g, value, 0, select, 1) == 0, "an option takes an integer");
    int added = graph_add_run_pin(&g, select);
    const pin_spec *option = added >= 0 ? &select->input_pins[added] : NULL;
    test_check(
        added == 2 && !strcmp(option->name, "Option1") && option->type == PIN_I32 &&
            option->group == select->input_pins[1].group &&
            (option->flags & PIN_FLAG_WILDCARD),
        "a new Select option joins its group with the resolved type"
    );
    test_check(
        graph_remove_run_pin(&g, select, 0, 1) == 0 && acount(g.edges) == 0 &&
            names(select, 0, (const char *[]){ "Index", "Option0" }, 2),
        "removing a wired option takes its wire and renumbers the rest"
    );
    node *array = add(&g, "Make Array");
    test_check(
        graph_add_run_pin(&g, array) == 1 && graph_add_run_pin(&g, array) == 2 &&
            names(array, 0, (const char *[]){ "Item", "Item1", "Item2" }, 3) &&
            array->input_pins[2].group == array->output_pins[0].group,
        "Make Array items number after the first"
    );
    node *format = graph_add_node(&g, &NODE_Format_Text);
    node *real = add(&g, "Real");
    test_check(
        graph_connect(&g, real, 0, format, 1) == 0 &&
            graph_add_run_pin(&g, format) == 2 &&
            !strcmp(format->input_pins[2].name, "Arg1") &&
            format->input_pins[2].type == PIN_FLOW,
        "a Format Text argument starts generic"
    );
    node *gate = graph_add_node(&g, &NODE_MultiGate);
    test_check(
        graph_add_run_pin(&g, gate) == 2 && !strcmp(gate->output_pins[2].name, "Out2"),
        "MultiGate grows its outputs"
    );
    graph_free(&g);
}

static void pin_links()
{
    graph g = { 0 };
    graph_init(&g);
    node *source = add(&g, "Real");
    node *a = add(&g, "Add");
    node *b = add(&g, "Add");
    graph_connect(&g, source, 0, a, 0);
    graph_connect(&g, source, 0, b, 0);
    test_check(
        graph_break_pin_links(&g, source, 1, 0) == 2 && acount(g.edges) == 0,
        "breaking an output removes every wire it drives"
    );
    graph_connect(&g, source, 0, a, 1);
    uint64_t folded = source->id;
    test_check(
        graph_node_is_inline(&g, source) && graph_break_pin_links(&g, a, 0, 1) == 1 &&
            !graph_node_by_id(&g, folded) && acount(g.nodes) == 2,
        "breaking an input drops the source folded into it"
    );
    pin_value five = { .type = PIN_F64, .type_name = "double", .d = 5 };
    graph_set_pin_default(&g, a, 0, 0, &five);
    pin_value reset = graph_pin_reset_value(a, 0, 0);
    test_check(
        reset.type == a->input_pins[0].type && reset.d == 0 &&
            graph_set_pin_default(&g, a, 0, 0, &reset) == 0 &&
            a->input_pins[0].default_value.d == 0,
        "an input resets to its declared default"
    );
    graph_free(&g);
}

static void promotion()
{
    behavior_asset *asset = behavior_asset_new("Promotion");
    graph *function = behavior_asset_add_function(asset, "Scale", 0);
    node *add_node = add(function, "Add");
    int before = acount(function->nodes);
    behavior_variable *v = behavior_asset_promote_pin_to_variable(asset, add_node, 0, 1, "Factor");
    node *get = acount(function->nodes) > before ? alast(function->nodes) : NULL;
    node *driver = NULL;
    int pin = -1;
    test_check(
        v && get && get->desc == &NODE_GetVariable && get->reference_id == v->id &&
            graph_input_source(function, add_node, 1, &driver, &pin) && driver == get,
        "a promoted pin reads its variable in the pin's own graph"
    );
    behavior_asset_free(asset);
}

static bool at(const align_box *box, float x, float y)
{
    return fabsf(box->x - x) < 1e-4f && fabsf(box->y - y) < 1e-4f;
}

static void alignment()
{
    align_box boxes[3] = { { 0, 10, 100, 40 }, { 50, 0, 60, 20 }, { 200, 100, 20, 80 } };
    align_box left[3], right[3], middle[3], across[3];
    memcpy(left, boxes, sizeof boxes);
    memcpy(right, boxes, sizeof boxes);
    memcpy(middle, boxes, sizeof boxes);
    memcpy(across, boxes, sizeof boxes);
    align_boxes(left, 3, ALIGN_LEFT);
    align_boxes(right, 3, ALIGN_RIGHT);
    align_boxes(middle, 3, ALIGN_MIDDLE);
    test_check(at(&left[0], 0, 10) && at(&left[1], 0, 0) && at(&left[2], 0, 100), "Align Left moves boxes to the leftmost edge");
    test_check(
        at(&right[0], 120, 10) && at(&right[1], 160, 0) && at(&right[2], 200, 100),
        "Align Right lines up right edges on the rightmost"
    );
    test_check(
        at(&middle[0], 0, 70) && at(&middle[1], 50, 80) && at(&middle[2], 200, 50),
        "Align Middle centers boxes on the bounds' vertical center"
    );
    distribute_boxes(across, 3, false);
    test_check(
        at(&across[0], 0, 10) && at(&across[1], 120, 0) && at(&across[2], 200, 100),
        "distributing leaves equal gaps between the outermost boxes"
    );
    align_box pair[2] = { { 0, 0, 10, 10 }, { 30, 5, 10, 10 } };
    distribute_boxes(pair, 2, true);
    test_check(at(&pair[0], 0, 0) && at(&pair[1], 30, 5), "two boxes have nothing to distribute");
}

void run_graph_editing_checks()
{
    sequence_runs();
    switch_runs();
    wildcard_runs();
    pin_links();
    promotion();
    alignment();
}
