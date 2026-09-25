#include "editor/workspace/editor.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void reload_keeps_document_identity()
{
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
    puts("editor documents PASS: explicit Reload replaces cached data and subsequent edits retain independent Undo/Redo");
}

static void nested_callers_survive_document_visits()
{
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
    assert(editor_open_graph_call(e, root_call_id) == 0);
    assert(editor_open_graph_call(e, inner_call_id) == 0);
    ed_select_node(e, graph_node_by_id(e->active_graph, value_id), 0);
    behavior_asset *other = behavior_asset_new("OtherCallers");
    assert(editor_visit_behavior_asset(e, other, other->graph->graph_id, "OtherCallers") == 0);
    assert(editor_graph_history_count(e) == 0);
    assert(editor_visit_behavior_asset(e, source, inner_id, "Callers") == 0);
    assert(editor_active_graph(e)->graph_id == inner_id);
    assert(editor_graph_history_count(e) == 2);
    assert(editor_graph_history_id(e, 0) == root_id);
    assert(editor_graph_history_id(e, 1) == outer_id);
    assert(acount(e->selected) == 1 && e->selected[0]->id == value_id);
    assert(editor_graph_back(e) == 0);
    assert(editor_active_graph(e)->graph_id == outer_id);
    assert(acount(e->selected) == 1 && e->selected[0]->id == inner_call_id);
    assert(editor_graph_back(e) == 0);
    assert(editor_active_graph(e)->graph_id == root_id);
    assert(editor_graph_back(e) == -1);
    editor_free(e);
    behavior_asset_free(source);
    behavior_asset_free(other);
    puts("editor documents PASS: nested caller ancestry and selection survive document visits");
}

void test_editor_documents()
{
    reload_keeps_document_identity();
    nested_callers_survive_document_visits();
}
