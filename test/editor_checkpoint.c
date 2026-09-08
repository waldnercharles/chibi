#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/editor_internal.h"
#include "graph/model/registry.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static editor *restore_selection(editor *source)
{
    char *json = editor_checkpoint(source);
    editor *candidate = editor_new(behavior_asset_new("Selection candidate"));
    assert(editor_restore_checkpoint(candidate, json));
    free(json);
    return candidate;
}

static void selection_checkpoint()
{
    behavior_asset *asset = behavior_asset_new("Selections");
    behavior_variable *variable = behavior_asset_add_variable(
        asset,
        "Count",
        PIN_I32,
        NULL,
        PIN_CONTAINER_SINGLE,
        &(pin_value){ .type = PIN_I32, .i = 1 },
        NULL
    );
    assert(variable);
    uint64_t variable_id = variable->id;
    node *literal = graph_add_node(asset->graph, registry_node_by_name("Integer"));
    node *relay = graph_add_node(asset->graph, registry_node_by_name("Reroute Data"));
    assert(graph_connect_data(asset->graph, literal, 0, relay, 0) == 0);
    const edge *wire = &asset->graph->edges[0];
    ed_link_ref link = { wire->from_id, wire->from_pin_id, wire->to_id, wire->to_pin_id };
    uint64_t graph_id = asset->graph->graph_id;
    editor *source = editor_new(asset);
    ed_select_variable(source, variable);
    editor *candidate = restore_selection(source);
    assert(candidate->selected_variable && candidate->selected_variable->id == variable_id);
    editor_free(candidate);
    behavior_asset *other = behavior_asset_new("Other selection document");
    assert(editor_visit_behavior_asset(source, other, other->graph->graph_id, "other") == 0);
    candidate = restore_selection(source);
    assert(editor_visit_behavior_asset(candidate, NULL, graph_id, "Selections") == 0);
    assert(candidate->selected_variable && candidate->selected_variable->id == variable_id);
    ed_select_node(candidate, NULL, 0);
    ed_add_selected_link(candidate, &link);
    editor *linked = restore_selection(candidate);
    assert(!linked->selected_variable && acount(linked->selected_links) == 1);
    assert(ed_edge_index(linked, &linked->selected_links[0]) == 0);
    assert(editor_visit_behavior_asset(linked, other, other->graph->graph_id, "other") == 0);
    editor *cached = restore_selection(linked);
    assert(editor_visit_behavior_asset(cached, NULL, graph_id, "Selections") == 0);
    assert(
        acount(cached->selected_links) == 1 &&
        ed_edge_index(cached, &cached->selected_links[0]) == 0
    );
    editor_free(cached);
    editor_free(linked);
    editor_free(candidate);
    editor_free(source);
    behavior_asset_free(other);
}

void test_editor_checkpoint()
{
    ImGui_CreateContext(NULL);
    editor_ui_initialize(false);
    selection_checkpoint();
    editor *source = editor_new(behavior_asset_new("Checkpoint A"));
    const node_desc *integer = registry_node_by_name("Integer");
    node *first = editor_spawn(source, integer, 12, 23);
    assert(first);
    uint64_t first_id = first->id;
    uint64_t graph_a = editor_active_graph(source)->graph_id;
    assert(editor_spawn(source, integer, 50, 60));
    editor_undo(source);
    editor_location first_location = { .graph_id = graph_a,
                                       .zoom = 0.6f,
                                       .pan_x = 13,
                                       .pan_y = -72,
                                       .nodes = &first_id,
                                       .node_count = 1 };
    assert(editor_restore_location(source, &first_location) == 0);
    behavior_asset *other = behavior_asset_new("Checkpoint B");
    uint64_t graph_b = other->graph->graph_id;
    assert(editor_visit_behavior_asset(source, other, graph_b, "B") == 0);
    node *selected = editor_spawn(source, integer, -10, 14);
    uint64_t selected_id = selected->id;
    editor_location location = { .graph_id = graph_b,
                                 .zoom = 0.75f,
                                 .pan_x = -43,
                                 .pan_y = 92,
                                 .nodes = &selected_id,
                                 .node_count = 1 };
    assert(editor_restore_location(source, &location) == 0);
    char *checkpoint = editor_checkpoint(source);
    int fonts = ImGui_GetIO()->Fonts->Fonts.Size;
    for (int iteration = 0; iteration < 3; iteration++) {
        editor *restored = editor_new(behavior_asset_new("Candidate"));
        editor_ui_initialize(false);
        assert(ImGui_GetIO()->Fonts->Fonts.Size == fonts);
        assert(!editor_restore_checkpoint(restored, "{broken"));
        assert(!editor_restore_checkpoint(restored, "{\"version\":999}"));
        assert(editor_restore_checkpoint(restored, checkpoint));
        editor_location actual = editor_capture_location(restored);
        assert(actual.graph_id == graph_b && actual.node_count == 1);
        assert(actual.nodes[0] == selected_id && actual.pan_x == -43 && actual.pan_y == 92);
        assert(actual.zoom == 0.75f);
        free(actual.nodes);
        editor_query query;
        editor_query_state(restored, &query);
        assert(query.dirty);
        editor_undo(restored);
        assert(acount(editor_active_graph(restored)->nodes) == 0);
        editor_query_state(restored, &query);
        assert(!query.dirty);
        editor_redo(restored);
        assert(graph_node_by_id(editor_active_graph(restored), selected_id));
        assert(editor_visit_behavior_asset(restored, NULL, graph_a, "Checkpoint A") == 0);
        actual = editor_capture_location(restored);
        assert(actual.node_count == 1 && actual.nodes[0] == first_id);
        assert(actual.pan_x == 13 && actual.pan_y == -72 && actual.zoom == 0.6f);
        free(actual.nodes);
        assert(acount(editor_active_graph(restored)->nodes) == 1);
        assert(graph_node_by_id(editor_active_graph(restored), first_id));
        editor_redo(restored);
        assert(acount(editor_active_graph(restored)->nodes) == 2);
        editor_undo(restored);
        editor_undo(restored);
        editor_query_state(restored, &query);
        assert(!query.dirty);
        editor_free(restored);
    }
    free(checkpoint);
    editor_free(source);
    behavior_asset_free(other);
    editor_ui_deactivate();
    ImGui_DestroyContext(NULL);
    printf("PASS: editor checkpoints preserve active and cached documents, baselines, Undo/Redo, node/link/variable selection and camera without adding fonts\n");
}
