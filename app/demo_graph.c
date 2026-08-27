#include "demo_graph.h"

#include "../core/registry.h"

#include <string.h>

int demo_graph_seed(graph *g)
{
    const node_desc *cc = registry_node_by_name("Real");
    const node_desc *add = registry_node_by_name("add_floats");
    if (!cc || !add) {
        return -1;
    }

    node *c1 = graph_add_node(g, cc);
    node *c2 = graph_add_node(g, cc);
    node *a = graph_add_node(g, add);
    if (!c1 || !c2 || !a) {
        return -1;
    }

    pin_value two = { .type = PIN_F64, .type_name = "double", .d = 2.0 };
    pin_value three = { .type = PIN_F64, .type_name = "double", .d = 3.0 };
    if (graph_set_pin_default(g, c1, 0, 0, &two)) {
        return -1;
    }
    if (graph_set_pin_default(g, c2, 0, 0, &three)) {
        return -1;
    }

    c1->x = -260.0f;
    c1->y = -50.0f;
    c2->x = -260.0f;
    c2->y = 50.0f;
    a->x = 20.0f;
    a->y = 0.0f;

    if (graph_connect(g, c1, 0, a, 0)) {
        return -1;
    }
    if (graph_connect(g, c2, 0, a, 1)) {
        return -1;
    }
    return 0;
}

static node *flow_demo_node(graph *g, const char *name, float x, float y)
{
    const node_desc *desc = registry_node_by_name(name);
    node *n = desc ? graph_add_node(g, desc) : NULL;
    if (n) {
        n->x = x;
        n->y = y;
    }
    return n;
}

int demo_graph_seed_flow(graph *g)
{
    node *event = flow_demo_node(g, "Event", -560.0f, -120.0f);
    node *sequence = flow_demo_node(g, "Sequence", -360.0f, -120.0f);
    node *delay = flow_demo_node(g, "Delay", -140.0f, -120.0f);
    node *branch = flow_demo_node(g, "Branch", 80.0f, -120.0f);
    node *condition = flow_demo_node(g, "Bool", -140.0f, 100.0f);
    node *loop = flow_demo_node(g, "ForLoop", 300.0f, -120.0f);
    node *body = flow_demo_node(g, "Sequence", 540.0f, -220.0f);
    node *completed = flow_demo_node(g, "Sequence", 540.0f, 20.0f);
    node *third = flow_demo_node(g, "Sequence", 300.0f, 300.0f);
    node *alternate = flow_demo_node(g, "Branch", -120.0f, 300.0f);
    if (!event || !sequence || !delay || !branch || !condition || !loop ||
        !body || !completed || !third || !alternate) {
        return -1;
    }
    pin_spec sequence_extra = { "Then2", PIN_FLOW, NULL, PIN_EXEC };
    if (graph_add_pin(g, sequence, 1, &sequence_extra) < 0) {
        return -1;
    }

    if (graph_set_event_name(g, event, "Begin")) {
        return -1;
    }
    pin_value true_value = { .type = PIN_BOOL, .b = true };
    if (graph_set_pin_default(g, condition, 0, 0, &true_value)) {
        return -1;
    }
    pin_value delay_duration = { .type = PIN_F64, .type_name = "double", .d = 1.0 };
    pin_value loop_first = { .type = PIN_I32, .type_name = "int", .i = 2 };
    pin_value loop_last = { .type = PIN_I32, .type_name = "int", .i = 4 };
    if (graph_set_pin_default(g, delay, 0, 1, &delay_duration) ||
        graph_set_pin_default(g, loop, 0, 1, &loop_first) ||
        graph_set_pin_default(g, loop, 0, 2, &loop_last)) {
        return -1;
    }

    if (graph_connect_exec(g, event, 0, sequence, 0, 0)) {
        return -1;
    }
    if (graph_connect_exec(g, sequence, 0, delay, 0, 0)) {
        return -1;
    }
    if (graph_connect_exec(g, delay, 0, branch, 0, 0)) {
        return -1;
    }
    if (graph_connect_data(g, condition, 0, branch, 1)) {
        return -1;
    }
    if (graph_connect_exec(g, branch, 0, loop, 0, 0)) {
        return -1;
    }
    if (graph_connect_exec(g, loop, 0, body, 0, 0)) {
        return -1;
    }
    if (graph_connect_exec(g, loop, 2, completed, 0, 0)) {
        return -1;
    }
    if (graph_connect_exec(g, sequence, 1, alternate, 0, 0)) {
        return -1;
    }
    if (graph_connect_data(g, condition, 0, alternate, 1)) {
        return -1;
    }
    if (graph_connect_exec(g, sequence, 2, third, 0, 0)) {
        return -1;
    }
    return 0;
}

static node *event_node(blueprint *bp, const char *name, float x, float y)
{
    node *event = graph_add_node(bp->graph, registry_node_by_name("Event"));
    if (!event || graph_set_event_name(bp->graph, event, name)) {
        return NULL;
    }
    event->x = x;
    event->y = y;
    return event;
}

int demo_graph_seed_event_demo(blueprint *bp)
{
    if (!bp) {
        return -1;
    }
    blueprint_variable *health = blueprint_add_variable(
        bp,
        "Health",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 100 },
        "State"
    );
    blueprint_variable *last_action = blueprint_add_variable(
        bp,
        "LastAction",
        PIN_NAME,
        "name",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_NAME, .str = "None" },
        "Input"
    );
    if (!health || !last_action) {
        return -1;
    }

    node *begin = event_node(bp, "Begin", -560.0f, -160.0f);
    node *set_health = blueprint_add_set_node(bp, health->id);
    node *tick = event_node(bp, "Tick", -560.0f, 40.0f);
    node *get_health = blueprint_add_get_node(bp, health->id);
    node *to_string = graph_add_node(bp->graph, registry_node_by_name("To String"));
    node *format = graph_add_node(bp->graph, registry_node_by_name("Format Text"));
    node *print = graph_add_node(bp->graph, registry_node_by_name("Print String"));
    node *input = event_node(bp, "Input", -560.0f, 260.0f);
    node *set_action = blueprint_add_set_node(bp, last_action->id);
    if (!begin || !set_health || !tick || !get_health || !to_string ||
        !format || !print || !input || !set_action) {
        return -1;
    }

    graph_set_pin_default(bp->graph, set_health, 0, 1, &(pin_value){ .type = PIN_I32, .i = 50 });
    graph_set_pin_type(bp->graph, to_string, 0, 0, PIN_I32, "int");
    graph_set_pin_default(bp->graph, format, 0, 0, &(pin_value){ .type = PIN_STR, .str = "Health={0}" });
    graph_connect_exec(bp->graph, begin, 0, set_health, 0, 0);
    graph_connect_exec(bp->graph, tick, 0, print, 0, 0);
    graph_connect_data(bp->graph, get_health, 0, to_string, 0);
    graph_connect_data(bp->graph, to_string, 0, format, 1);
    graph_connect_data(bp->graph, format, 0, print, 1);

    pin_spec delta = { "DeltaSeconds", PIN_F64, "double", PIN_DATA };
    graph_add_pin(bp->graph, tick, 1, &delta);
    pin_spec action = { "Action", PIN_NAME, "name", PIN_DATA };
    pin_spec value = { "Value", PIN_F64, "double", PIN_DATA };
    pin_spec pressed = { "Pressed", PIN_BOOL, "bool", PIN_DATA };
    graph_add_pin(bp->graph, input, 1, &action);
    graph_add_pin(bp->graph, input, 1, &value);
    graph_add_pin(bp->graph, input, 1, &pressed);
    graph_connect_exec(bp->graph, input, 0, set_action, 0, 0);
    graph_connect_data(bp->graph, input, 1, set_action, 1);
    return 0;
}
