#include "editor/workspace/scene_navigation.h"

#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void reload_keeps_document_identity()
{
    ImGuiContext *context = ImGui_CreateContext(NULL);
    behavior_asset *initial = behavior_asset_new("Reload");
    behavior_asset *disk = behavior_asset_new("Reload");
    behavior_asset *other = behavior_asset_new("Other");
    behavior_asset_set_description(disk, "Saved graph");
    editor *e = editor_new(initial);
    assert(editor_spawn(e, registry_node_by_name("Integer"), 0, 0));
    uint64_t graph_id = disk->graph->graph_id;
    assert(editor_reload_behavior_asset(e, disk, graph_id, "Reload") == 0);
    assert(strcmp(editor_behavior_asset(e)->description, "Saved graph") == 0);
    editor_undo(e);
    assert(acount(editor_active_graph(e)->nodes) == 0);
    assert(editor_spawn(e, registry_node_by_name("Integer"), 0, 0));
    assert(editor_visit_behavior_asset(e, other, other->graph->graph_id, "Other") == 0);
    assert(editor_visit_behavior_asset(e, disk, graph_id, "Reload") == 0);
    assert(acount(editor_active_graph(e)->nodes) == 1);
    editor_undo(e);
    assert(acount(editor_active_graph(e)->nodes) == 0);
    editor_redo(e);
    assert(acount(editor_active_graph(e)->nodes) == 1);
    behavior_asset_set_description((behavior_asset *)editor_behavior_asset(e), "Unsaved replacement");
    assert(editor_reload_behavior_asset(e, disk, graph_id, "Reload") == 0);
    assert(strcmp(editor_behavior_asset(e)->description, "Saved graph") == 0);
    assert(acount(editor_active_graph(e)->nodes) == 0);
    editor_free(e);
    behavior_asset_free(disk);
    behavior_asset_free(other);
    ImGui_DestroyContext(context);
    puts("scene navigation PASS: explicit Reload replaces cached data and subsequent edits retain independent Undo/Redo");
}

static void nested_callers_survive_workspace_visits()
{
    ImGuiContext *context = ImGui_CreateContext(NULL);
    behavior_asset *asset = behavior_asset_new("Callers");
    graph *outer = behavior_asset_add_function(asset, "Outer", 0);
    graph *inner = behavior_asset_add_function(asset, "Inner", 0);
    node *root_call = behavior_asset_add_function_call_node(asset, outer);
    node *inner_call = behavior_asset_add_function_call_node_to_graph(asset, outer, inner);
    node *value = graph_add_node(inner, registry_node_by_name("Integer"));
    assert(root_call && inner_call && value);
    uint64_t root_id = asset->graph->graph_id;
    uint64_t outer_id = outer->graph_id;
    uint64_t inner_id = inner->graph_id;
    uint64_t root_call_id = root_call->id;
    uint64_t inner_call_id = inner_call->id;
    uint64_t value_id = value->id;
    char *json = behavior_asset_save_json(asset);
    behavior_asset *source = behavior_asset_load_json(json);
    free(json);
    editor *e = editor_new(asset);
    editor_location root = { .graph_id = root_id, .pan_x = 21, .pan_y = -19, .zoom = 0.7f };
    assert(editor_restore_location(e, &root) == 0);
    assert(editor_open_graph_call(e, root_call_id) == 0);
    editor_location middle = editor_capture_location(e);
    middle.pan_x = -60;
    middle.pan_y = 43;
    middle.zoom = 1.4f;
    assert(editor_restore_location(e, &middle) == 0);
    assert(editor_open_graph_call(e, inner_call_id) == 0);
    editor_location leaf = editor_capture_location(e);
    leaf.nodes = malloc(sizeof *leaf.nodes);
    leaf.nodes[0] = value_id;
    leaf.node_count = 1;
    leaf.pan_x = 130;
    leaf.pan_y = -80;
    leaf.zoom = 0.8f;
    assert(editor_restore_location(e, &leaf) == 0);
    assert(leaf.caller_count == 2);
    behavior_asset *other = behavior_asset_new("OtherCallers");
    assert(editor_visit_behavior_asset(e, other, other->graph->graph_id, "OtherCallers") == 0);
    assert(editor_can_visit_location(e, source, "Callers", &leaf));
    assert(editor_visit_behavior_asset(e, source, root_id, "Callers") == 0);
    assert(editor_restore_location(e, &leaf) == 0);
    assert(editor_active_graph(e)->graph_id == inner_id);
    assert(editor_graph_history_count(e) == 2);
    assert(editor_graph_history_id(e, 0) == root_id);
    assert(editor_graph_history_id(e, 1) == outer_id);
    editor_location restored = editor_capture_location(e);
    assert(restored.node_count == 1 && restored.nodes[0] == value_id);
    assert(restored.pan_x == 130 && restored.pan_y == -80 && restored.zoom == 0.8f);
    free(restored.nodes);
    assert(editor_graph_back(e) == 0);
    editor_query query;
    editor_query_state(e, &query);
    assert(
        query.active_graph->graph_id == outer_id && query.pan_x == -60 &&
        query.pan_y == 43 && query.zoom == 1.4f
    );
    assert(editor_graph_back(e) == 0);
    editor_query_state(e, &query);
    assert(
        query.active_graph->graph_id == root_id && query.pan_x == 21 &&
        query.pan_y == -19 && query.zoom == 0.7f
    );

    behavior_asset *working = (behavior_asset *)editor_behavior_asset(e);
    graph_remove_node(working->graph, graph_node_by_id(working->graph, root_call_id));
    assert(editor_restore_location(e, &leaf) == 0);
    assert(editor_graph_history_count(e) == 1 && editor_graph_history_id(e, 0) == outer_id);
    graph *current_outer = behavior_asset_find_graph(working, outer_id);
    graph_remove_node(current_outer, graph_node_by_id(current_outer, inner_call_id));
    assert(editor_restore_location(e, &leaf) == 0);
    assert(editor_graph_history_count(e) == 0);
    assert(editor_open_graph(e, root_id) == 0);
    graph *current_inner = behavior_asset_find_graph(working, inner_id);
    graph_remove_node(current_inner, graph_node_by_id(current_inner, value_id));
    assert(!editor_can_visit_location(e, source, "Callers", &leaf));
    assert(editor_restore_location(e, &leaf) == -1);
    assert(editor_active_graph(e)->graph_id == root_id);
    leaf.graph_id = UINT64_MAX;
    assert(!editor_can_visit_location(e, source, "Callers", &leaf));
    assert(editor_restore_location(e, &leaf) == -1);
    free(middle.nodes);
    free(leaf.nodes);
    editor_free(e);
    behavior_asset_free(source);
    behavior_asset_free(other);
    ImGui_DestroyContext(context);
    puts("scene navigation PASS: nested caller ancestry, selection and views survive visits; deleted callers trim safely and deleted targets preserve the active graph");
}

static void checkpoint_navigation()
{
    scene_navigation *source = scene_navigation_new(),
                     *restored = scene_navigation_new();
    uint64_t node = 9;
    ecs_entity old = { .slot = 4, .generation = 1 },
               replacement = { .slot = 6, .generation = 2 };
    scene_location first = {
        .kind = 2,
        .document = "First",
        .entity = old,
        .camera = { 10, 20, 300, 200 },
        .graph = { .graph_id = 5,
                   .nodes = &node,
                   .node_count = 1,
                   .pan_x = 123,
                   .zoom = 1,
                   .caller_count = 1,
                   .callers = { { .graph_id = 2, .node_id = 3, .zoom = 0.5f } } }
    };
    scene_navigation_record(source, &first);
    first.document = "Second";
    scene_navigation_record(source, &first);
    char *json = scene_navigation_checkpoint(source);
    uint64_t before = ecs_entity_to_u64(old), after = ecs_entity_to_u64(replacement);
    assert(scene_navigation_restore_checkpoint(restored, json, &before, &after, 1));
    const scene_location *location = scene_navigation_step(restored, false);
    assert(location && !strcmp(location->document, "First"));
    assert(ecs_entity_to_u64(location->entity) == after);
    assert(location->graph.nodes[0] == 9 && location->graph.callers[0].node_id == 3);
    assert(location->camera.center_x == 10 && location->graph.pan_x == 123);
    assert(!scene_navigation_restore_checkpoint(restored, "{}", NULL, NULL, 0));
    assert(scene_navigation_can_move(restored, true));
    free(json);
    scene_navigation_free(source);
    scene_navigation_free(restored);
}

void test_scene_navigation()
{
    checkpoint_navigation();
    reload_keeps_document_identity();
    nested_callers_survive_workspace_visits();
    scene_navigation *history = scene_navigation_new();
    uint64_t node = 7;
    scene_location first = {
        .kind = 1,
        .document = "First",
        .tab = 0,
        .graph = { .graph_id = 3, .nodes = &node, .node_count = 1, .zoom = 1 },
    };
    scene_navigation_record(history, &first);
    first.graph.pan_x = 123;
    scene_navigation_record(history, &first);
    assert(!scene_navigation_can_move(history, false));
    scene_location second = first;
    second.document = "Second";
    scene_navigation_record(history, &second);
    node = 100;
    const scene_location *location = scene_navigation_step(history, false);
    assert(location && strcmp(location->document, "First") == 0);
    assert(location->graph.pan_x == 123 && location->graph.nodes[0] == 7);
    assert(scene_navigation_can_move(history, true));
    scene_location branch = first;
    branch.document = "Branch";
    scene_navigation_record(history, &branch);
    assert(!scene_navigation_can_move(history, true));
    location = scene_navigation_step(history, false);
    assert(location && strcmp(location->document, "First") == 0);
    scene_navigation_discard_current(history, false);
    assert(!scene_navigation_can_move(history, false));
    scene_navigation_free(history);

    history = scene_navigation_new();
    first.graph.nodes = NULL;
    first.graph.node_count = 0;
    for (int i = 0; i < 140; i++) {
        first.tab = i;
        scene_navigation_record(history, &first);
    }
    int visited = 1;
    while (scene_navigation_step(history, false)) {
        visited++;
    }
    assert(visited == 128);
    scene_navigation_free(history);
    puts("scene navigation PASS: copied selection, view updates, Back/Forward, branching, invalid removal, and bounded history");
}
