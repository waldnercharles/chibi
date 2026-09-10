#include "editor/workspace/scene_debug.h"

#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <stdlib.h>
#include <string.h>

void scene_debug_clear(scene_debug *debug)
{
    free(debug->asset_name);
    *debug = (scene_debug){ 0 };
}

void scene_debug_select(scene_debug *debug, const game_behavior_attachment_info *info, uint64_t session)
{
    char *name = xstrdup(info->asset_name);
    scene_debug_clear(debug);
    debug->entity = info->entity;
    debug->sequence = info->sequence;
    debug->session = session;
    debug->asset_name = name;
}

bool scene_debug_resolve(
    const scene_debug *debug,
    ecs_world *world,
    game_behavior_world *behaviors,
    uint64_t session,
    game_behavior_attachment_info *out
)
{
    if (!debug->asset_name || debug->session != session ||
        !ecs_entity_is_alive(world, debug->entity)) {
        return false;
    }
    int count = game_behavior_attachment_count_for_entity(behaviors, debug->entity);
    for (int i = 0; i < count; i++) {
        game_behavior_attachment_info info;
        if (game_behavior_attachment_info_at(behaviors, debug->entity, i, &info) == 0 &&
            info.sequence == debug->sequence &&
            !strcmp(info.asset_name, debug->asset_name)) {
            *out = info;
            return true;
        }
    }
    return false;
}

static void normalize_pins(yyjson_mut_doc *doc, yyjson_mut_val *pins)
{
    size_t index, count;
    yyjson_mut_val *pin;
    yyjson_mut_arr_foreach(pins, index, count, pin)
    {
        yyjson_mut_obj_remove_key(pin, "group");
        const char *name = yyjson_mut_get_str(yyjson_mut_obj_get(pin, "type_name"));
        if (!name) {
            continue;
        }
        /* Linking resolves aliases; their spelling does not change execution. */
        for (int depth = 0; depth < 16; depth++) {
            const type_desc *type = registry_type_by_name(name);
            if (!type || !type->alias_of || !strcmp(type->alias_of, name)) {
                break;
            }
            name = type->alias_of;
        }
        yyjson_mut_obj_remove_key(pin, "type_name");
        yyjson_mut_obj_add_strcpy(doc, pin, "type_name", name);
    }
}

static yyjson_mut_doc *execution_json(const behavior_asset *asset)
{
    char *json = behavior_asset_save_json(asset);
    if (!json) {
        return NULL;
    }
    yyjson_doc *source = yyjson_read(json, strlen(json), 0);
    free(json);
    if (!source) {
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(source, NULL);
    yyjson_doc_free(source);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_obj_remove_key(root, "id_counter");
    yyjson_mut_obj_remove_key(root, "description");
    yyjson_mut_val *graphs = yyjson_mut_obj_get(root, "graphs");
    for (int index = acount(asset->graphs) - 1; index >= 0; index--) {
        if (asset->graphs[index]->source_asset) {
            yyjson_mut_arr_remove(graphs, (size_t)index);
        }
    }
    size_t i, count;
    yyjson_mut_val *graph;
    yyjson_mut_arr_foreach(graphs, i, count, graph)
    {
        yyjson_mut_obj_remove_key(graph, "comment_boxes");
        yyjson_mut_val *nodes = yyjson_mut_obj_get(graph, "nodes");
        size_t j, node_count;
        yyjson_mut_val *node;
        yyjson_mut_arr_foreach(nodes, j, node_count, node)
        {
            normalize_pins(doc, yyjson_mut_obj_get(node, "inputs"));
            normalize_pins(doc, yyjson_mut_obj_get(node, "outputs"));
            yyjson_mut_obj_remove_key(node, "x");
            yyjson_mut_obj_remove_key(node, "y");
            uint64_t target_id = yyjson_mut_get_uint(yyjson_mut_obj_get(node, "reference_id"));
            for (int index = 0; target_id && index < acount(asset->graphs); index++) {
                const struct graph *target = asset->graphs[index];
                if (target->graph_id != target_id || !target->source_asset) {
                    continue;
                }
                yyjson_mut_obj_remove_key(node, "reference_id");
                yyjson_mut_obj_remove_key(node, "reference_asset");
                yyjson_mut_obj_add_uint(doc, node, "reference_id", target->source_graph_id);
                yyjson_mut_obj_add_str(doc, node, "reference_asset", target->source_asset);
                break;
            }
        }
        yyjson_mut_val *edges = yyjson_mut_obj_get(graph, "edges");
        size_t k, edge_count;
        yyjson_mut_val *edge;
        yyjson_mut_arr_foreach(edges, k, edge_count, edge)
        {
            const char *keys[] = { "reroutes",
                                   "from_handle_x",
                                   "from_handle_y",
                                   "to_handle_x",
                                   "to_handle_y" };
            for (int field = 0; field < 5; field++) {
                yyjson_mut_obj_remove_key(edge, keys[field]);
            }
        }
    }
    return doc;
}

bool scene_debug_matches(const behavior_asset *working, const behavior_asset *applied)
{
    yyjson_mut_doc *authored = execution_json(working);
    yyjson_mut_doc *runtime = execution_json(applied);
    bool matches = authored && runtime &&
                   yyjson_mut_equals(yyjson_mut_doc_get_root(authored), yyjson_mut_doc_get_root(runtime));
    yyjson_mut_doc_free(authored);
    yyjson_mut_doc_free(runtime);
    return matches;
}

bool scene_debug_matches_cached(
    scene_debug *debug,
    const behavior_asset *working,
    unsigned revision,
    const behavior_asset *applied
)
{
    if (!debug->comparison.valid || debug->comparison.working != working ||
        debug->comparison.revision != revision || debug->comparison.applied != applied) {
        debug->comparison.matches = scene_debug_matches(working, applied);
        debug->comparison.working = working;
        debug->comparison.applied = applied;
        debug->comparison.revision = revision;
        debug->comparison.valid = true;
    }
    return debug->comparison.matches;
}
