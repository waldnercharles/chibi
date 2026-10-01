#include "editor/workspace/scene_debug.h"

#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "support/asset_value.h"
#include "support/xalloc.h"

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

static const char *resolved_alias(const char *name)
{
    for (int depth = 0; name && depth < 16; depth++) {
        const type_desc *type = registry_type_by_name(name);
        if (!type || !type->alias_of || !strcmp(type->alias_of, name)) {
            break;
        }
        name = type->alias_of;
    }
    return name;
}

/* Type aliases name the same type; pin groups and declarations only guide
 * editing. None changes execution. */
static void normalize_pins(behavior_asset *asset)
{
    for (int g = 0; g < acount(asset->graphs); g++) {
        for (int i = 0; i < acount(asset->graphs[g]->nodes); i++) {
            node *n = asset->graphs[g]->nodes[i];
            for (int output = 0; output < 2; output++) {
                pin_spec *pins = output ? n->output_pins : n->input_pins;
                int count = output ? n->output_count : n->input_count;
                for (int p = 0; p < count; p++) {
                    pins[p].group = 0;
                    pins[p].type_name = resolved_alias(pins[p].type_name);
                    pins[p].declaration_id = serialize_call_declaration(asset, n, output, p);
                }
            }
        }
    }
}

/* Linking imports library graphs at the ID counter, so calls into them
 * compare by the library's IDs and the imports themselves drop out. */
static av_doc *execution_value(const behavior_asset *source_asset)
{
    behavior_asset *asset = behavior_asset_clone(source_asset);
    if (!asset) {
        return NULL;
    }
    normalize_pins(asset);
    av_doc *doc = av_doc_new();
    av_value *root = behavior_asset_to_value(doc, asset);
    av_doc_set_root(doc, root);
    av_obj_remove_key(root, "id_counter");
    av_obj_remove_key(root, "description");
    av_value *graphs = av_obj_get(root, "graphs");
    for (int index = acount(asset->graphs) - 1; index >= 0; index--) {
        if (asset->graphs[index]->source_asset) {
            av_arr_remove(graphs, (size_t)index);
        }
    }
    size_t i, count;
    av_value *graph;
    av_arr_foreach(graphs, i, count, graph)
    {
        av_obj_remove_key(graph, "comment_boxes");
        av_value *nodes = av_obj_get(graph, "nodes");
        size_t j, node_count;
        av_value *node;
        av_arr_foreach(nodes, j, node_count, node)
        {
            av_obj_remove_key(node, "x");
            av_obj_remove_key(node, "y");
            av_obj_remove_key(node, "placed");
            uint64_t target_id = av_get_uint(av_obj_get(node, "reference_id"));
            for (int index = 0; target_id && index < acount(asset->graphs); index++) {
                const struct graph *target = asset->graphs[index];
                if (target->graph_id != target_id || !target->source_asset) {
                    continue;
                }
                av_obj_remove_key(node, "reference_id");
                av_obj_remove_key(node, "reference_asset");
                av_obj_add_uint(doc, node, "reference_id", target->source_graph_id);
                av_obj_add_strcpy(doc, node, "reference_asset", target->source_asset);
                break;
            }
        }
        av_value *edges = av_obj_get(graph, "edges");
        size_t k, edge_count;
        av_value *edge;
        av_arr_foreach(edges, k, edge_count, edge)
        {
            const char *keys[] = { "reroutes",
                                   "from_handle_x",
                                   "from_handle_y",
                                   "to_handle_x",
                                   "to_handle_y" };
            for (int field = 0; field < 5; field++) {
                av_obj_remove_key(edge, keys[field]);
            }
        }
    }
    behavior_asset_free(asset);
    return doc;
}

bool scene_debug_matches(const behavior_asset *linked, const behavior_asset *applied)
{
    av_doc *authored = execution_value(linked);
    av_doc *runtime = execution_value(applied);
    bool matches = authored && runtime &&
                   av_equals(av_doc_root(authored), av_doc_root(runtime));
    av_doc_free(authored);
    av_doc_free(runtime);
    return matches;
}

bool scene_debug_runs(game_live_world *live, const behavior_asset *working, const behavior_asset *applied)
{
    behavior_asset *linked = game_live_link(live, working);
    bool runs = linked && scene_debug_matches(linked, applied);
    behavior_asset_free(linked);
    return runs;
}

bool scene_debug_runs_cached(
    scene_debug *debug,
    game_live_world *live,
    const behavior_asset *working,
    unsigned revision,
    const behavior_asset *applied
)
{
    if (!debug->comparison.valid || debug->comparison.working != working ||
        debug->comparison.revision != revision || debug->comparison.applied != applied) {
        debug->comparison.matches = scene_debug_runs(live, working, applied);
        debug->comparison.working = working;
        debug->comparison.applied = applied;
        debug->comparison.revision = revision;
        debug->comparison.valid = true;
    }
    return debug->comparison.matches;
}
