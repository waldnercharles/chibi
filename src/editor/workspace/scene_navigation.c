#include "editor/workspace/scene_navigation.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

enum
{
    NAVIGATION_LIMIT = 128
};

struct scene_navigation
{
    scene_location *locations;
    int cursor;
};

static bool same_text(const char *a, const char *b)
{
    return a && b ? strcmp(a, b) == 0 : a == b;
}

static bool same_location(const scene_location *a, const scene_location *b)
{
    bool same_scene_identity = a->scene_key && b->scene_key;
    bool same_document_identity = a->document_key && b->document_key;
    if (a->kind != b->kind || a->tab != b->tab ||
        ecs_entity_to_u64(a->entity) != ecs_entity_to_u64(b->entity) ||
        a->graph.graph_id != b->graph.graph_id ||
        a->graph.node_count != b->graph.node_count ||
        !same_text(a->document, b->document) || !same_text(a->scene_key, b->scene_key) ||
        !same_text(a->document_key, b->document_key) ||
        (!same_scene_identity && !same_text(a->scene_path, b->scene_path)) ||
        (!same_document_identity && !same_text(a->system_path, b->system_path)) ||
        !same_text(a->prototype, b->prototype) || !same_text(a->content, b->content) ||
        !same_text(a->attachment, b->attachment)) {
        return false;
    }
    for (int i = 0; i < a->graph.node_count; i++) {
        if (a->graph.nodes[i] != b->graph.nodes[i]) {
            return false;
        }
    }
    return true;
}

static void free_location(scene_location *location)
{
    free((char *)location->document);
    free((char *)location->document_key);
    free((char *)location->scene_key);
    free((char *)location->scene_path);
    free((char *)location->system_path);
    free((char *)location->prototype);
    free((char *)location->content);
    free((char *)location->attachment);
    free(location->graph.nodes);
}

static scene_location copy_location(const scene_location *source)
{
    scene_location copy = *source;
    copy.document = source->document ? xstrdup(source->document) : NULL;
    copy.document_key = source->document_key ? xstrdup(source->document_key) : NULL;
    copy.scene_key = source->scene_key ? xstrdup(source->scene_key) : NULL;
    copy.scene_path = source->scene_path ? xstrdup(source->scene_path) : NULL;
    copy.system_path = source->system_path ? xstrdup(source->system_path) : NULL;
    copy.prototype = source->prototype ? xstrdup(source->prototype) : NULL;
    copy.content = source->content ? xstrdup(source->content) : NULL;
    copy.attachment = source->attachment ? xstrdup(source->attachment) : NULL;
    copy.graph.nodes = NULL;
    if (source->graph.node_count) {
        size_t size = (size_t)source->graph.node_count * sizeof *copy.graph.nodes;
        copy.graph.nodes = xmalloc(size);
        memcpy(copy.graph.nodes, source->graph.nodes, size);
    }
    return copy;
}

scene_navigation *scene_navigation_new()
{
    scene_navigation *navigation = xcalloc(1, sizeof *navigation);
    navigation->cursor = -1;
    return navigation;
}

void scene_navigation_free(scene_navigation *navigation)
{
    for (int i = 0; i < acount(navigation->locations); i++) {
        free_location(&navigation->locations[i]);
    }
    afree(navigation->locations);
    free(navigation);
}

void scene_navigation_record(scene_navigation *navigation, const scene_location *location)
{
    if (navigation->cursor >= 0 &&
        same_location(&navigation->locations[navigation->cursor], location)) {
        scene_location copy = copy_location(location);
        free_location(&navigation->locations[navigation->cursor]);
        navigation->locations[navigation->cursor] = copy;
        return;
    }
    for (int i = navigation->cursor + 1; i < acount(navigation->locations); i++) {
        free_location(&navigation->locations[i]);
    }
    asetlen(navigation->locations, navigation->cursor + 1);
    apush(navigation->locations, copy_location(location));
    navigation->cursor++;
    if (acount(navigation->locations) > NAVIGATION_LIMIT) {
        free_location(&navigation->locations[0]);
        adel(navigation->locations, 0);
        navigation->cursor--;
    }
}

void scene_navigation_remap_entities(
    scene_navigation *navigation,
    const char *scene_key,
    const uint64_t *before,
    const uint64_t *after,
    int count
)
{
    if (!scene_key || !before || !after || count <= 0) {
        return;
    }
    for (int i = 0; i < acount(navigation->locations); i++) {
        scene_location *location = &navigation->locations[i];
        if (!location->scene_key || strcmp(location->scene_key, scene_key) != 0) {
            continue;
        }
        uint64_t entity = ecs_entity_to_u64(location->entity);
        for (int j = 0; j < count; j++) {
            if (entity == before[j]) {
                location->entity = ecs_entity_from_u64(after[j]);
                break;
            }
        }
    }
}

bool scene_navigation_can_move(const scene_navigation *navigation, bool forward)
{
    return forward ? navigation->cursor + 1 < acount(navigation->locations)
                   : navigation->cursor > 0;
}

const scene_location *scene_navigation_step(scene_navigation *navigation, bool forward)
{
    if (!scene_navigation_can_move(navigation, forward)) {
        return NULL;
    }
    navigation->cursor += forward ? 1 : -1;
    return &navigation->locations[navigation->cursor];
}

void scene_navigation_discard_current(scene_navigation *navigation, bool forward)
{
    free_location(&navigation->locations[navigation->cursor]);
    adel(navigation->locations, navigation->cursor);
    if (forward) {
        navigation->cursor--;
    }
}

#include "editor/workspace/checkpoint.h"

static yyjson_mut_val *navigation_graph(yyjson_mut_doc *doc, const editor_location *graph)
{
    yyjson_mut_val *object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, object, "id", graph->graph_id);
    yyjson_mut_obj_add_bool(doc, object, "fit", graph->fit);
    yyjson_mut_obj_add_int(doc, object, "entry", graph->entry_index);
    yyjson_mut_obj_add_real(doc, object, "zoom", graph->zoom);
    yyjson_mut_obj_add_real(doc, object, "x", graph->pan_x);
    yyjson_mut_obj_add_real(doc, object, "y", graph->pan_y);
    yyjson_mut_val *nodes = yyjson_mut_obj_add_arr(doc, object, "nodes");
    for (int i = 0; i < graph->node_count; i++) {
        yyjson_mut_arr_add_uint(doc, nodes, graph->nodes[i]);
    }
    yyjson_mut_val *callers = yyjson_mut_obj_add_arr(doc, object, "callers");
    for (int i = 0; i < graph->caller_count; i++) {
        const editor_caller_location *caller = &graph->callers[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, callers);
        yyjson_mut_obj_add_uint(doc, item, "graph", caller->graph_id);
        yyjson_mut_obj_add_uint(doc, item, "node", caller->node_id);
        yyjson_mut_obj_add_real(doc, item, "x", caller->pan_x);
        yyjson_mut_obj_add_real(doc, item, "y", caller->pan_y);
        yyjson_mut_obj_add_real(doc, item, "zoom", caller->zoom);
        yyjson_mut_obj_add_int(doc, item, "entry", caller->entry_index);
        yyjson_mut_obj_add_bool(doc, item, "fit", caller->fit);
    }
    return object;
}

static bool navigation_read_graph(yyjson_val *object, editor_location *graph)
{
    if (!yyjson_is_obj(object)) {
        return false;
    }
    graph->graph_id = yyjson_get_uint(yyjson_obj_get(object, "id"));
    graph->fit = yyjson_get_bool(yyjson_obj_get(object, "fit"));
    graph->entry_index = yyjson_get_int(yyjson_obj_get(object, "entry"));
    graph->zoom = yyjson_get_num(yyjson_obj_get(object, "zoom"));
    graph->pan_x = yyjson_get_num(yyjson_obj_get(object, "x"));
    graph->pan_y = yyjson_get_num(yyjson_obj_get(object, "y"));
    yyjson_val *nodes = yyjson_obj_get(object, "nodes"),
               *callers = yyjson_obj_get(object, "callers");
    if (!yyjson_is_arr(nodes) || !yyjson_is_arr(callers) ||
        yyjson_arr_size(callers) > EDITOR_LOCATION_CALLER_LIMIT) {
        return false;
    }
    graph->node_count = yyjson_arr_size(nodes);
    graph->nodes = graph->node_count
                     ? xcalloc(graph->node_count, sizeof *graph->nodes)
                     : NULL;
    for (int i = 0; i < graph->node_count; i++) {
        yyjson_val *node = yyjson_arr_get(nodes, i);
        if (!yyjson_is_uint(node)) {
            return false;
        }
        graph->nodes[i] = yyjson_get_uint(node);
    }
    graph->caller_count = yyjson_arr_size(callers);
    for (int i = 0; i < graph->caller_count; i++) {
        yyjson_val *item = yyjson_arr_get(callers, i);
        if (!yyjson_is_obj(item)) {
            return false;
        }
        graph->callers[i] = (editor_caller_location){
            .graph_id = yyjson_get_uint(yyjson_obj_get(item, "graph")),
            .node_id = yyjson_get_uint(yyjson_obj_get(item, "node")),
            .pan_x = yyjson_get_num(yyjson_obj_get(item, "x")),
            .pan_y = yyjson_get_num(yyjson_obj_get(item, "y")),
            .zoom = yyjson_get_num(yyjson_obj_get(item, "zoom")),
            .entry_index = yyjson_get_int(yyjson_obj_get(item, "entry")),
            .fit = yyjson_get_bool(yyjson_obj_get(item, "fit")),
        };
    }
    return isfinite(graph->zoom) && isfinite(graph->pan_x) && isfinite(graph->pan_y);
}

char *scene_navigation_checkpoint(const scene_navigation *navigation)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    yyjson_mut_obj_add_int(doc, root, "cursor", navigation->cursor);
    yyjson_mut_val *locations = yyjson_mut_obj_add_arr(doc, root, "locations");
    for (int i = 0; i < acount(navigation->locations); i++) {
        const scene_location *location = &navigation->locations[i];
        yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, locations);
        yyjson_mut_obj_add_int(doc, object, "kind", location->kind);
        yyjson_mut_obj_add_int(doc, object, "tab", location->tab);
        yyjson_mut_obj_add_uint(
            doc,
            object,
            "entity",
            ecs_entity_to_u64(location->entity)
        );
        checkpoint_text(doc, object, "document", location->document);
        checkpoint_text(doc, object, "document_key", location->document_key);
        checkpoint_text(doc, object, "scene_key", location->scene_key);
        checkpoint_text(doc, object, "scene", location->scene_path);
        checkpoint_text(doc, object, "system", location->system_path);
        checkpoint_text(doc, object, "prototype", location->prototype);
        checkpoint_text(doc, object, "content", location->content);
        checkpoint_text(doc, object, "attachment", location->attachment);
        yyjson_mut_obj_add_val(
            doc,
            object,
            "camera",
            checkpoint_camera(doc, location->camera)
        );
        yyjson_mut_obj_add_val(
            doc,
            object,
            "graph",
            navigation_graph(doc, &location->graph)
        );
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

bool scene_navigation_restore_checkpoint(
    scene_navigation *navigation,
    const char *json,
    const uint64_t *before,
    const uint64_t *after,
    int count
)
{
    yyjson_doc *doc = json ? yyjson_read(json, strlen(json), 0) : NULL;
    yyjson_val *root = yyjson_doc_get_root(doc),
               *locations = yyjson_obj_get(root, "locations");
    if (yyjson_get_int(yyjson_obj_get(root, "version")) != 1 ||
        !yyjson_is_arr(locations) || yyjson_arr_size(locations) > NAVIGATION_LIMIT) {
        yyjson_doc_free(doc);
        return false;
    }
    scene_navigation *candidate = scene_navigation_new();
    candidate->cursor = yyjson_get_int(yyjson_obj_get(root, "cursor"));
    bool valid = candidate->cursor >= -1 &&
                 candidate->cursor < (int)yyjson_arr_size(locations);
    size_t i, size;
    yyjson_val *object;
    yyjson_arr_foreach(locations, i, size, object)
    {
        if (!valid) {
            break;
        }
        apush(candidate->locations, ((scene_location){ 0 }));
        scene_location *location = &alast(candidate->locations);
        location->kind = yyjson_get_int(yyjson_obj_get(object, "kind"));
        location->tab = yyjson_get_int(yyjson_obj_get(object, "tab"));
        location->entity = checkpoint_entity(
            yyjson_get_uint(yyjson_obj_get(object, "entity")),
            before,
            after,
            count
        );
        location->document = checkpoint_copy_text(object, "document");
        location->document_key = checkpoint_copy_text(object, "document_key");
        location->scene_key = checkpoint_copy_text(object, "scene_key");
        location->scene_path = checkpoint_copy_text(object, "scene");
        location->system_path = checkpoint_copy_text(object, "system");
        location->prototype = checkpoint_copy_text(object, "prototype");
        location->content = checkpoint_copy_text(object, "content");
        location->attachment = checkpoint_copy_text(object, "attachment");
        valid = location->document && location->kind >= 0 && location->kind <= 3 &&
                checkpoint_read_camera(
                    yyjson_obj_get(object, "camera"),
                    &location->camera
                ) &&
                navigation_read_graph(yyjson_obj_get(object, "graph"), &location->graph);
    }
    yyjson_doc_free(doc);
    if (valid) {
        scene_navigation old = *navigation;
        *navigation = *candidate;
        *candidate = old;
    }
    scene_navigation_free(candidate);
    return valid;
}
