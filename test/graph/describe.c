#include "graph/authoring/describe.h"

#include "graph/nodes/component_nodes.h"
#include "test/suite_internal.h"

#include <assert.h>

static bool describes(const node_subject *subject, const char *text)
{
    char about[256];
    return node_subject_describe(subject, about, sizeof about) && !strcmp(about, text);
}

static bool placed_describes(const node *n, const behavior_asset *const *catalog, int count, const char *text)
{
    node_subject subject = node_subject_of(n, catalog, count);
    return describes(&subject, text);
}

static node *configured(graph *g, const node_desc *desc, const char *setting)
{
    node *n = graph_add_node(g, desc);
    pin_value key = { .type = PIN_NAME, .str = setting };
    assert(graph_set_pin_default(g, n, 0, component_node_setting(n), &key) == 0);
    return n;
}

/* Hover text for placed nodes and for search results: a node type's
   sentence, a configured component node's, and an event's own. */
void run_graph_describe_checks()
{
    behavior_asset *asset = behavior_asset_new("Described");
    graph *g = asset->graph;
    behavior_asset *hit = behavior_asset_new("Hit");
    hit->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    behavior_asset_set_description(hit, "An entity was struck.");
    behavior_asset *quiet = behavior_asset_new("Quiet");
    quiet->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    const behavior_asset *catalog[] = { hit, quiet };

    node *branch = graph_add_node(g, &NODE_Branch);
    test_check(placed_describes(branch, catalog, 2, NODE_Branch.description), "describe: a node reads its type's sentence");

    const ecs_component_desc *health = &GAME_COMPONENT_GameHealth;
    const char *type = health->type->name;
    char text[256];
    snprintf(text, sizeof text, "Reads %s from Entity, which defaults to Self.", type);
    node *get = configured(g, &NODE_GetComponent, health->key);
    test_check(placed_describes(get, catalog, 2, text), "describe: a configured Get Component names its component");
    node *unset = graph_add_node(g, &NODE_GetComponent);
    test_check(
        placed_describes(unset, catalog, 2, NODE_GetComponent.description),
        "describe: a component node without a setting reads its type's sentence"
    );
    snprintf(text, sizeof text, "Builds a %s from its fields.", type);
    node *make = configured(g, &NODE_MakeStruct, type);
    test_check(placed_describes(make, catalog, 2, text), "describe: a Make Struct names its struct");
    const char *field = health->type->fields[0].name;
    snprintf(text, sizeof text, "Writes %s's %s on Entity, which defaults to Self.", type, field);
    node_subject preset = { .desc = &NODE_SetComponent,
                            .setting = health->key,
                            .field = field };
    test_check(describes(&preset, text), "describe: a Set preset names the field it writes");

    node *tick = graph_add_node(g, &NODE_Event);
    graph_set_event_name(g, tick, graph_host_event_name(GRAPH_HOST_EVENT_TICK));
    test_check(
        placed_describes(
            tick,
            catalog,
            2,
            GRAPH_HOST_EVENTS[GRAPH_HOST_EVENT_TICK].description
        ),
        "describe: an engine event reads its own sentence"
    );
    node *on_hit = graph_add_node(g, &NODE_Event);
    graph_set_event_name(g, on_hit, "Hit");
    test_check(
        placed_describes(on_hit, catalog, 2, "An entity was struck."),
        "describe: an event definition's handler reads its description"
    );
    node *on_quiet = graph_add_node(g, &NODE_Event);
    graph_set_event_name(g, on_quiet, "Quiet");
    test_check(
        placed_describes(on_quiet, catalog, 2, NODE_Event.description),
        "describe: a definition without a description reads the Event node's"
    );
    node *send = graph_add_node(g, registry_node_by_name("Send Event"));
    assert(graph_set_node_reference(g, send, 0, "Hit") == 0);
    test_check(placed_describes(send, catalog, 2, "Sends Hit to Entity."), "describe: Send Event names the event it sends");
    node_subject host = { .desc = &NODE_Event,
                          .host = &GRAPH_HOST_EVENTS[GRAPH_HOST_EVENT_CONTACT_BEGIN] };
    test_check(
        describes(&host, GRAPH_HOST_EVENTS[GRAPH_HOST_EVENT_CONTACT_BEGIN].description),
        "describe: a search result for an engine event reads the event's "
        "sentence"
    );
    behavior_asset_free(quiet);
    behavior_asset_free(hit);
    behavior_asset_free(asset);
}
