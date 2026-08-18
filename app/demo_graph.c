#include "demo_graph.h"

#include "core/registry.h"

int demo_graph_seed(graph *g)
{
    const node_desc *cc = registry_node_by_name("const_f32");
    const node_desc *add = registry_node_by_name("add_floats");
    if (!cc || !add) return -1;

    node *c1 = graph_add_node(g, cc);
    node *c2 = graph_add_node(g, cc);
    node *a = graph_add_node(g, add);
    if (!c1 || !c2 || !a) return -1;

    c1->outputs[0].type = PIN_F32;
    c1->outputs[0].type_name = "float";
    c1->outputs[0].f = 2.0f;
    c2->outputs[0].type = PIN_F32;
    c2->outputs[0].type_name = "float";
    c2->outputs[0].f = 3.0f;

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
