#include "scene_debug.h"

#include "core/serialize.h"
#include "core/xalloc.h"
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

static char *execution_json(const behavior_asset *asset)
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
            yyjson_mut_obj_remove_key(node, "x");
            yyjson_mut_obj_remove_key(node, "y");
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
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

bool scene_debug_matches(const behavior_asset *working, const behavior_asset *applied)
{
    char *authored = execution_json(working);
    char *runtime = execution_json(applied);
    bool matches = authored && runtime && !strcmp(authored, runtime);
    free(authored);
    free(runtime);
    return matches;
}
