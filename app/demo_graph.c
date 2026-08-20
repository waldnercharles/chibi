#include "demo_graph.h"

#include "../core/registry.h"

int demo_graph_seed(graph *g)
{
    const node_desc *cc = registry_node_by_name("Real");
    const node_desc *add = registry_node_by_name("add_floats");
    if (!cc || !add) return -1;

    node *c1 = graph_add_node(g, cc);
    node *c2 = graph_add_node(g, cc);
    node *a = graph_add_node(g, add);
    if (!c1 || !c2 || !a) return -1;

    pin_value two = { .type = PIN_F64, .type_name = "double", .d = 2.0 };
    pin_value three = { .type = PIN_F64, .type_name = "double", .d = 3.0 };
    if (graph_set_pin_default(g, c1, 0, 0, &two)) return -1;
    if (graph_set_pin_default(g, c2, 0, 0, &three)) return -1;

    c1->x = -260.0f;
    c1->y = -50.0f;
    c2->x = -260.0f;
    c2->y = 50.0f;
    a->x = 20.0f;
    a->y = 0.0f;

    if (graph_connect(g, c1, 0, a, 0)) return -1;
    if (graph_connect(g, c2, 0, a, 1)) return -1;
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
        !body || !completed || !third || !alternate)
        return -1;
    pin_spec sequence_extra = { "Then2", PIN_FLOW, NULL, PIN_EXEC };
    if (graph_add_pin(g, sequence, 1, &sequence_extra) < 0) return -1;

    if (graph_set_event_name(g, event, "OnStart")) return -1;
    pin_value true_value = { .type = PIN_BOOL, .b = true };
    if (graph_set_pin_default(g, condition, 0, 0, &true_value)) return -1;
    delay->inputs[1].d = 1.0;
    loop->inputs[1].i = 2;
    loop->inputs[2].i = 4;

    if (graph_connect_exec(g, event, 0, sequence, 0, 0)) return -1;
    if (graph_connect_exec(g, sequence, 0, delay, 0, 0)) return -1;
    if (graph_connect_exec(g, delay, 0, branch, 0, 0)) return -1;
    if (graph_connect_data(g, condition, 0, branch, 1)) return -1;
    if (graph_connect_exec(g, branch, 0, loop, 0, 0)) return -1;
    if (graph_connect_exec(g, loop, 0, body, 0, 0)) return -1;
    if (graph_connect_exec(g, loop, 2, completed, 0, 0)) return -1;
    if (graph_connect_exec(g, sequence, 1, alternate, 0, 0)) return -1;
    if (graph_connect_data(g, condition, 0, alternate, 1)) return -1;
    if (graph_connect_exec(g, sequence, 2, third, 0, 0)) return -1;
    return 0;
}
