#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/nodes/core_nodes.h"
#include "support/array.h"
#include "test/graph/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

static behavior_asset *asset, *loaded;
static char *document;

static void cleanup()
{
    free(document);
    document = NULL;
    behavior_asset_free(loaded);
    behavior_asset_free(asset);
    asset = loaded = NULL;
}

/* Every authored value of a Behavior Asset has a model surface that saves:
   variables, literals, comment frames and graph settings. */
TEST_CASE(asset_documents_keep_authored_surfaces)
{
    asset = behavior_asset_new("EditorSurface");
    node *event = graph_add_node(asset->graph, &NODE_Event);
    REQUIRE(graph_set_event_name(asset->graph, event, "Surface") == 0);
    REQUIRE(event->output_count > 0);
    pin_value three = { .type = PIN_I32, .i = 3 };
    REQUIRE(
        behavior_asset_add_variable(asset, "Value", PIN_I32, "int", PIN_CONTAINER_SINGLE, &three, "Editor")
    );
    node *literal = graph_add_node(asset->graph, graph_palette_node("Integer"));
    REQUIRE(graph_set_node_disabled(asset->graph, literal, true) == 0);
    /* Defaults are omitted, so author non-default graph state. */
    graph_comment_box *notes = graph_add_comment_box(asset->graph, "Notes", 0.0f, 0.0f, 200.0f, 100.0f);
    asset->graph->breakpoints_enabled = 0;
    document = graph_asset_text(asset);
    REQUIRE(document);
    loaded = graph_asset_read(document);
    REQUIRE(loaded && !loaded->graph->breakpoints_enabled);
    REQUIRE(acount(loaded->variables) == 1 && loaded->variables[0].default_value.i == 3);
    REQUIRE(!strcmp(loaded->variables[0].category, "Editor"));
    REQUIRE(graph_node_by_id(loaded->graph, literal->id)->disabled);
    REQUIRE(acount(loaded->graph->comment_boxes) == 1);
    const graph_comment_box *saved = &loaded->graph->comment_boxes[0];
    REQUIRE(saved->id == notes->id && !strcmp(saved->title, "Notes"));
    REQUIRE(saved->x == 0.0f && saved->y == 0.0f);
    REQUIRE(
        saved->width == 200.0f && saved->height == 100.0f &&
        saved->color == notes->color
    );
    REQUIRE(graph_palette_node("Format Text")->keywords);
    REQUIRE(behavior_asset_set_owner_type(asset, "EditorOwner") == 0 && asset->owner_type_name);
    return true;
}

/* Function graphs and their pure and executable calls. */
TEST_CASE(function_calls_mirror_their_signatures)
{
    asset = behavior_asset_new("Functions");
    REQUIRE(asset->graph == asset->graphs[0] && asset->graph->kind == GRAPH_EVENT);
    graph *fn = behavior_asset_add_function(asset, "Identity", 0);
    REQUIRE(fn && fn->kind == GRAPH_FUNCTION && fn->owner == asset);
    REQUIRE(acount(asset->graphs) == 2 && fn->entry_id && fn->return_id);
    REQUIRE(fn->call_depth_limit > 0);
    node *entry = graph_node_by_id(fn, fn->entry_id);
    node *ret = graph_node_by_id(fn, fn->return_id);
    pin_spec arg = { "Value", PIN_I32, "int", PIN_DATA };
    REQUIRE(graph_add_pin(fn, entry, 1, &arg) >= 0 && graph_add_pin(fn, ret, 0, &arg) >= 0);
    REQUIRE(graph_connect_exec(fn, entry, 0, ret, 0, 0) == 0);
    REQUIRE(graph_connect_data(fn, entry, 1, ret, 1) == 0);
    REQUIRE(graph_exec_input_count(fn, ret, 0) == 1);
    REQUIRE(entry->output_count == 2 && ret->input_count == 2);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    graph_set_event_name(asset->graph, event, "CallIdentity");
    node *call = behavior_asset_add_function_call_node(asset, fn);
    pin_value answer = { .type = PIN_I32, .i = 42 };
    REQUIRE(graph_set_pin_default(asset->graph, call, 0, 1, &answer) == 0);
    REQUIRE(graph_connect_exec(asset->graph, event, 0, call, 0, 0) == 0);
    REQUIRE(call->reference_id == fn->graph_id);
    REQUIRE(call->input_count == 2 && call->output_count == 2);
    REQUIRE(NODE_Delay.latent != 0);

    graph *pure = behavior_asset_add_function(asset, "PureIdentity", 1);
    node *pure_entry = graph_node_by_id(pure, pure->entry_id);
    node *pure_ret = graph_node_by_id(pure, pure->return_id);
    REQUIRE(
        graph_add_pin(pure, pure_entry, 1, &arg) >= 0 &&
        graph_add_pin(pure, pure_ret, 0, &arg) >= 0
    );
    REQUIRE(graph_connect_data(pure, pure_entry, 0, pure_ret, 0) == 0);
    node *pure_call = behavior_asset_add_function_call_node(asset, pure);
    REQUIRE(pure_call && pure_call->desc == &NODE_CallPureFunction);
    REQUIRE(acount(asset->graphs) == 3);

    /* Local call pins derive their declarations from the signature by name. */
    document = graph_asset_text(asset);
    REQUIRE(document && !strstr(document, "declaration_id"));
    loaded = graph_asset_read(document);
    REQUIRE(loaded && acount(loaded->graphs) == 3);
    REQUIRE(behavior_asset_find_graph_by_name(loaded, "PureIdentity"));
    node *loaded_call = graph_node_by_id(loaded->graph, call->id);
    REQUIRE(loaded_call);
    REQUIRE(loaded_call->input_pins[1].declaration_id == entry->output_pins[1].id);
    REQUIRE(loaded_call->output_pins[1].declaration_id == ret->input_pins[1].id);
    return true;
}

void test_graph_functions()
{
    graph_palette_open();
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(asset_documents_keep_authored_surfaces);
    RUN_TEST_CASE(function_calls_mirror_their_signatures);
    pu_clear_setup();
    graph_palette_close();
}
