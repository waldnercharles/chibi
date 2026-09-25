#include "graph/authoring/find.h"

#include "graph/nodes/behavior_expression.h"
#include "test/suite_internal.h"

static bool finds(
    const behavior_asset *asset,
    const node *n,
    const char *title,
    const char *query,
    graph_find_field field,
    const char *text
)
{
    graph_find_match match;
    return graph_find_node(asset, asset->graph, n, title, query, &match) &&
           match.field == field && !strcmp(match.text, text);
}

/* Find in graph matches titles, pins, values, bindings, expression source
   and comments, in that order, ignoring case. */
void run_graph_find_checks()
{
    behavior_asset *asset = behavior_asset_new("FindBehavior");
    graph *g = asset->graph;
    node *real = graph_add_node(g, registry_node_by_name("Real"));
    node *add = graph_add_node(g, registry_node_by_name("Add"));
    graph_set_pin_default(g, real, 1, 0, &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 2.5 });
    /* Wiring settles the operator's type, so its values come after. */
    graph_connect(g, real, 0, add, 0);
    graph_set_pin_default(g, add, 0, 0, &(pin_value){ .type = PIN_F64, .type_name = "double", .d = 2.5 });
    pin_value seven = add->input_pins[1].default_value;
    if (seven.type == PIN_F32) {
        seven.f = 7.25f;
    } else {
        seven.d = 7.25;
    }
    graph_set_pin_default(g, add, 0, 1, &seven);
    graph_set_node_comment(g, add, "Scaled dash", false);
    behavior_expression_error error = { 0 };
    node *expression = behavior_expression_add(asset, g, "speed * 2", &error);
    const char *b = add->input_pins[1].name;
    char value[64];
    snprintf(value, sizeof value, "%s: 7.25", b);
    graph_find_match match;
    test_check(finds(asset, add, "Add", "ADD", GRAPH_FIND_TITLE, "Add"), "find: titles match ignoring case");
    test_check(finds(asset, add, "Add", "7.2", GRAPH_FIND_VALUE, value), "find: an unwired input's value matches under its pin");
    test_check(!graph_find_node(asset, g, add, "Add", "2.5", &match), "find: a wired input's stale value does not match");
    test_check(
        graph_find_node(asset, g, real, "Real", "2.5", &match) &&
            match.field == GRAPH_FIND_VALUE,
        "find: a literal's value matches"
    );
    test_check(finds(asset, add, "Add", "dash", GRAPH_FIND_COMMENT, "Scaled dash"), "find: comments match");
    test_check(
        expression &&
            finds(asset, expression, "Expression", "* 2", GRAPH_FIND_EXPRESSION, "speed * 2"),
        "find: expression source matches"
    );
    test_check(
        expression &&
            finds(asset, expression, "Expression", "SPEED", GRAPH_FIND_PIN, "speed"),
        "find: pin names match before the source"
    );
    pin_binding health = { .kind = PIN_BINDING_COMPONENT_FIELD,
                           .component_key = "GAME.GameHealth",
                           .field_name = "current" };
    test_check(
        expression && graph_set_pin_binding(g, expression, 0, 0, &health) == 0 &&
            finds(asset, expression, "Expression", "gamehealth.cur", GRAPH_FIND_BINDING, "speed: GAME.GameHealth.current"),
        "find: bound components and fields match under their pin"
    );
    test_check(!graph_find_node(asset, g, add, "Add", "", &match), "find: an empty query matches nothing");
    test_check(!graph_find_node(asset, g, add, "Add", "missing", &match), "find: unmatched queries find nothing");
    behavior_asset_free(asset);
}
