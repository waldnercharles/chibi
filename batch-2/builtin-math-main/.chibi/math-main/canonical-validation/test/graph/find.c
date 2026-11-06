#include "graph/authoring/find.h"

#include "graph/authoring/graph_edit.h"
#include "graph/nodes/behavior_expression.h"
#include "test/graph/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

static behavior_asset *asset;
static node *add, *real, *expression;

static void cleanup()
{
    behavior_asset_free(asset);
    asset = NULL;
}

/* Real(2.5) wired into Add(_, 7.25) with a comment, and the expression
   `speed * 2`. */
static void setup()
{
    asset = behavior_asset_new("FindBehavior");
    graph *g = asset->graph;
    real = graph_add_node(g, graph_palette_node("Real"));
    add = graph_add_node(g, graph_palette_node("Add"));
    pin_value half = { .type = PIN_F64, .type_name = "f64", .d = 2.5 };
    graph_set_pin_default(g, real, 1, 0, &half);
    /* Wiring settles the operator's type, so its values come after. */
    graph_connect(g, real, 0, add, 0);
    graph_set_pin_default(g, add, 0, 0, &half);
    pin_value seven = add->input_pins[1].default_value;
    if (seven.type == PIN_F32) {
        seven.f = 7.25f;
    } else {
        seven.d = 7.25;
    }
    graph_set_pin_default(g, add, 0, 1, &seven);
    graph_set_node_comment(g, add, "Scaled dash", false);
    behavior_expression_error error = { 0 };
    expression = behavior_expression_add(asset, g, "speed * 2", &error);
}

static bool finds(const node *n, const char *title, const char *query, graph_find_field field, const char *text)
{
    graph_find_match match;
    return graph_find_node(asset, asset->graph, n, title, query, &match) &&
           match.field == field && !strcmp(match.text, text);
}

/* Find in graph matches titles, pins, values, bindings, expression source
   and comments, in that order, ignoring case. */
TEST_CASE(find_matches_titles_values_and_comments)
{
    REQUIRE(finds(add, "Add", "ADD", GRAPH_FIND_TITLE, "Add"));
    char value[64];
    snprintf(value, sizeof value, "%s: 7.25", add->input_pins[1].name);
    REQUIRE(finds(add, "Add", "7.2", GRAPH_FIND_VALUE, value));
    graph_find_match match;
    REQUIRE(!graph_find_node(asset, asset->graph, add, "Add", "2.5", &match));
    REQUIRE(graph_find_node(asset, asset->graph, real, "Real", "2.5", &match));
    REQUIRE(match.field == GRAPH_FIND_VALUE);
    REQUIRE(finds(add, "Add", "dash", GRAPH_FIND_COMMENT, "Scaled dash"));
    REQUIRE(!graph_find_node(asset, asset->graph, add, "Add", "", &match));
    REQUIRE(!graph_find_node(asset, asset->graph, add, "Add", "missing", &match));
    return true;
}

TEST_CASE(find_matches_expression_pins_source_and_bindings)
{
    REQUIRE(expression);
    REQUIRE(finds(expression, "Expression", "* 2", GRAPH_FIND_EXPRESSION, "speed * 2"));
    REQUIRE(finds(expression, "Expression", "SPEED", GRAPH_FIND_PIN, "speed"));
    pin_binding health = { .kind = PIN_BINDING_COMPONENT_FIELD,
                           .component_key = "Health",
                           .field_name = "current" };
    REQUIRE(graph_set_pin_binding(asset->graph, expression, 0, 0, &health) == 0);
    REQUIRE(finds(expression, "Expression", "health.cur", GRAPH_FIND_BINDING, "speed: Health.current"));
    return true;
}

void test_graph_find()
{
    graph_palette_open();
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(find_matches_titles_values_and_comments);
    RUN_TEST_CASE(find_matches_expression_pins_source_and_bindings);
    pu_clear_setup();
    graph_palette_close();
}
