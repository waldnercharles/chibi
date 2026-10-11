#include "graph/authoring/describe.h"

#include "graph/authoring/graph_edit.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "runtime/component_catalog.h"
#include "test/graph/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static behavior_asset *asset, *hit, *quiet;
static const behavior_asset *catalog[2];

static void setup()
{
    asset = behavior_asset_new("Described");
    hit = behavior_asset_new("Hit");
    hit->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    behavior_asset_set_description(hit, "An entity was struck.");
    quiet = behavior_asset_new("Quiet");
    quiet->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    catalog[0] = hit;
    catalog[1] = quiet;
}

static void cleanup()
{
    behavior_asset_free(quiet);
    behavior_asset_free(hit);
    behavior_asset_free(asset);
    asset = hit = quiet = NULL;
}

static bool describes(const node_subject *subject, const char *text)
{
    char about[256];
    return node_subject_describe(subject, about, sizeof about) && !strcmp(about, text);
}

static bool placed_describes(const node *n, const char *text)
{
    node_subject subject = node_subject_of(n, catalog, 2);
    return describes(&subject, text);
}

static node *configured(const node_desc *desc, const char *setting)
{
    graph *g = asset->graph;
    node *n = graph_add_node(g, desc);
    pin_value key = { .type = PIN_NAME, .str = setting };
    int set = graph_set_pin_default(g, n, 0, component_node_setting(n), &key);
    assert(set == 0);
    (void)set;
    return n;
}

static node *handler(const char *event)
{
    node *n = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, n, event);
    return n;
}

/* Hover text for placed nodes and for search results: a node type's
   sentence, a configured component node's, and an event's own. */
TEST_CASE(nodes_read_their_types_and_components_sentences)
{
    REQUIRE(
        placed_describes(graph_add_node(asset->graph, &NODE_Branch), NODE_Branch.description)
    );
    const ecs_component_desc *health = &COMPONENT_Health;
    const char *type = health->type->name;
    char text[256];
    snprintf(text, sizeof text, "Reads %s from Entity, which defaults to Self.", type);
    REQUIRE(placed_describes(configured(&NODE_GetComponent, health->key), text));
    node *unset = graph_add_node(asset->graph, &NODE_GetComponent);
    REQUIRE(placed_describes(unset, NODE_GetComponent.description));
    snprintf(text, sizeof text, "Builds a %s from its fields.", type);
    REQUIRE(placed_describes(configured(&NODE_MakeStruct, type), text));
    const char *field = health->type->fields[0].name;
    snprintf(text, sizeof text, "Writes %s's %s on Entity, which defaults to Self.", type, field);
    node_subject preset = { .desc = &NODE_SetComponent,
                            .setting = health->key,
                            .field = field };
    REQUIRE(describes(&preset, text));
    return true;
}

TEST_CASE(events_read_their_own_sentences)
{
    const host_event_desc *tick = &HOST_EVENTS[HOST_EVENT_TICK];
    REQUIRE(placed_describes(handler(host_event_name(HOST_EVENT_TICK)), tick->description));
    REQUIRE(placed_describes(handler("Hit"), "An entity was struck."));
    REQUIRE(placed_describes(handler("Quiet"), NODE_Event.description));
    node *send = graph_add_node(asset->graph, graph_palette_node("Send Event"));
    REQUIRE(graph_set_node_reference(asset->graph, send, 0, "Hit") == 0);
    REQUIRE(placed_describes(send, "Sends Hit to Entity."));
    const host_event_desc *contact = &HOST_EVENTS[HOST_EVENT_CONTACT_BEGIN];
    node_subject host = { .desc = &NODE_Event, .host = contact };
    REQUIRE(describes(&host, contact->description));
    return true;
}

void test_graph_describe()
{
    graph_palette_open();
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(nodes_read_their_types_and_components_sentences);
    RUN_TEST_CASE(events_read_their_own_sentences);
    pu_clear_setup();
    graph_palette_close();
}
