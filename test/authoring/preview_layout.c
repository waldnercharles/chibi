#include "authoring/preview.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/events.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "test/support/graph_packs.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>

/* Arranging a preview definition's graph: automatic layout changes position
 * alone, while moving a node the author placed is an unsaved edit. */

static behavior_asset *open;
static preview_definition *definition;

static void cleanup()
{
    behavior_asset_free(open);
    open = NULL;
    preview_definition_free(definition);
    definition = NULL;
    test_graph_packs_clear();
}

/* A copy as an editor holds it. Executable source does not preserve node
 * IDs, so the copy goes through the authoring document instead. */
static behavior_asset *authoring_snapshot(const behavior_asset *asset)
{
    av_doc *doc = av_doc_new();
    behavior_asset *copy = behavior_asset_from_value(behavior_asset_to_value(doc, asset));
    av_doc_free(doc);
    return copy;
}

/* Begin and Tick each set a counter variable. */
static behavior_asset *counter_behavior()
{
    behavior_asset *asset = behavior_asset_new("Counter");
    behavior_variable *counter = behavior_asset_add_variable(
        asset,
        "Counter",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 0 },
        "Live"
    );
    node *begin = graph_add_node(asset->graph, registry_node_by_name("Event"));
    node *tick = graph_add_node(asset->graph, registry_node_by_name("Event"));
    node *set = counter ? behavior_asset_add_set_node(asset, counter->id) : NULL;
    if (behavior_asset_set_owner_type(asset, "ecs.entity") || !counter || !begin ||
        !tick || !set || graph_set_event_name(asset->graph, begin, "Begin") ||
        graph_set_event_name(asset->graph, tick, "Tick") ||
        graph_host_event_apply(asset->graph, begin) ||
        graph_host_event_apply(asset->graph, tick) ||
        graph_connect_exec(asset->graph, begin, 0, set, 0, 0) ||
        graph_connect_exec(asset->graph, tick, 0, set, 0, 0)) {
        behavior_asset_free(asset);
        return NULL;
    }
    return asset;
}

TEST_CASE(arranging_automatic_nodes_keeps_a_definition_saved)
{
    REQUIRE(test_graph_packs_set());
    behavior_asset *source = counter_behavior();
    REQUIRE(source);
    uint64_t automatic = source->graph->nodes[0]->id,
             placed = source->graph->nodes[1]->id;
    source->graph->nodes[0]->placed = false;
    definition = preview_definition_new("Counter", source);
    open = authoring_snapshot(definition->working);
    REQUIRE(open);
    graph_node_by_id(open->graph, automatic)->x += 240;
    REQUIRE(!preview_sync_working(definition, open));
    REQUIRE(!definition->dirty && !definition->unsaved);
    REQUIRE(
        graph_node_by_id(definition->working->graph, automatic)->x ==
        graph_node_by_id(open->graph, automatic)->x
    );
    /* Moving a placed node is an unsaved edit. */
    graph_node_by_id(open->graph, placed)->x += 40;
    REQUIRE(!preview_sync_working(definition, open));
    REQUIRE(definition->dirty && definition->unsaved);
    return true;
}

/* Component access nodes write one-line file entries that read the same
 * wherever arrangement puts them. */
TEST_CASE(arranged_component_access_nodes_keep_their_entries)
{
    static const char *const kinds[] = {
        "Get_Collider",
        "GetEntity_Collider",
        "Has_Collider",
        "HasEntity_Collider",
        "Set_Collider",
        "SetEntity_Collider",
        "Add_Collider",
        "Remove_Collider",
        "Make_Collider",
        "Break_Collider",
        "Set_Collider_ignore_entity",
    };
    static const float spots[][2] = { { 12, 0 }, { 1576, 261 }, { -1234567, -7654321 } };
    REQUIRE(test_graph_packs_set());
    behavior_asset *source = behavior_asset_new("Arranged");
    for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++) {
        node *n = graph_add_node(source->graph, registry_node_by_name(kinds[i]));
        if (!n) {
            behavior_asset_free(source);
            fprintf(stderr, "no %s node\n", kinds[i]);
        }
        REQUIRE(n);
        n->placed = false;
    }
    definition = preview_definition_new("Arranged", source);
    for (size_t s = 0; s < sizeof spots / sizeof *spots; s++) {
        open = authoring_snapshot(definition->working);
        REQUIRE(open);
        for (int i = 0; i < acount(open->graph->nodes); i++) {
            open->graph->nodes[i]->x = spots[s][0];
            open->graph->nodes[i]->y = spots[s][1];
        }
        REQUIRE(!preview_sync_working(definition, open) && !definition->unsaved);
        behavior_asset_free(open);
        open = NULL;
    }
    return true;
}

void test_preview_layout()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(arranging_automatic_nodes_keeps_a_definition_saved);
    RUN_TEST_CASE(arranged_component_access_nodes_keep_their_entries);
    pu_clear_setup();
}
