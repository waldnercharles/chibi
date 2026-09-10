#include "../third_party/yyjson/yyjson.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void ed_keep_document(editor *e)
{
    if (!e->document_key) {
        return;
    }
    ed_commit_snapshot(e);
    ed_save_graph_view(e);
    ed_document_history history = {
        .key = e->document_key,
        .json = behavior_asset_save_json(e->asset),
        .saved = e->saved_snapshot,
        .owner_identity = e->asset ? e->asset->identity : 0,
        .undo = e->undo,
        .redo = e->redo,
        .views = e->graph_views,
        .location = editor_capture_location(e),
        .variable_id = e->selected_variable ? e->selected_variable->id : 0,
    };
    for (int i = 0; i < acount(e->selected_links); i++) {
        apush(history.links, e->selected_links[i]);
    }
    apush(e->documents, history);
    e->document_key = NULL;
    e->saved_snapshot = NULL;
    e->undo = NULL;
    e->redo = NULL;
    e->graph_views = NULL;
}

int editor_visit_behavior_asset(editor *e, const behavior_asset *source, uint64_t graph_id, const char *key)
{
    if (e->document_key && strcmp(e->document_key, key) == 0) {
        return editor_open_graph(e, graph_id);
    }
    ed_document_history history = { 0 };
    int index = -1;
    for (int i = 0; i < acount(e->documents); i++) {
        if (strcmp(e->documents[i].key, key) == 0) {
            history = e->documents[i];
            index = i;
            break;
        }
    }
    behavior_asset *cached = history.json ? behavior_asset_load_json(history.json)
                                          : NULL;
    const behavior_asset *target = cached ? cached : source;
    if (!target || !behavior_asset_find_graph(target, graph_id)) {
        behavior_asset_free(cached);
        return -1;
    }
    if (cached && history.owner_identity) {
        cached->identity = history.owner_identity;
    }
    if (index >= 0) {
        adel(e->documents, index);
    }
    ed_keep_document(e);
    int result = editor_open_behavior_asset(e, target, graph_id);
    behavior_asset_free(cached);
    assert(result == 0);
    if (history.key) {
        ed_free_string_stack(&e->undo);
        ed_free_string_stack(&e->redo);
        free(e->saved_snapshot);
        e->undo = history.undo;
        e->redo = history.redo;
        e->saved_snapshot = history.saved;
        free(history.json);
        e->document_key = history.key;
        afree(e->graph_views);
        e->graph_views = history.views;
        if (history.location.graph_id == graph_id) {
            editor_restore_location(e, &history.location);
            e->selected_variable = behavior_asset_find_variable_by_id(
                e->asset,
                history.variable_id
            );
            for (int i = 0; i < acount(history.links); i++) {
                apush(e->selected_links, history.links[i]);
            }
        }
        free(history.location.nodes);
        afree(history.links);
    } else {
        e->document_key = xstrdup(key);
    }
    return 0;
}

int editor_reload_behavior_asset(editor *e, const behavior_asset *source, uint64_t graph_id, const char *key)
{
    char *copy = xstrdup(key);
    if (editor_open_behavior_asset(e, source, graph_id) != 0) {
        free(copy);
        return -1;
    }
    for (int i = 0; i < acount(e->documents); i++) {
        ed_document_history *history = &e->documents[i];
        if (strcmp(history->key, copy) != 0) {
            continue;
        }
        free(history->key);
        free(history->json);
        free(history->saved);
        ed_free_string_stack(&history->undo);
        ed_free_string_stack(&history->redo);
        afree(history->views);
        afree(history->links);
        free(history->location.nodes);
        adel(e->documents, i);
        break;
    }
    e->document_key = copy;
    return 0;
}

bool editor_can_visit_location(
    const editor *e,
    const behavior_asset *source,
    const char *key,
    const editor_location *location
)
{
    behavior_asset *cached = NULL;
    const behavior_asset *target = source;
    if (e->document_key && strcmp(e->document_key, key) == 0) {
        target = e->asset;
    } else {
        for (int i = 0; i < acount(e->documents); i++) {
            if (strcmp(e->documents[i].key, key) == 0) {
                cached = behavior_asset_load_json(e->documents[i].json);
                target = cached;
                break;
            }
        }
    }
    graph *graph = target ? behavior_asset_find_graph(target, location->graph_id)
                          : NULL;
    bool valid = graph != NULL;
    for (int i = 0; valid && i < location->node_count; i++) {
        valid = graph_node_by_id(graph, location->nodes[i]) != NULL;
    }
    behavior_asset_free(cached);
    return valid;
}

editor_location editor_capture_location(const editor *e)
{
    editor_location location = {
        .graph_id = e->active_graph ? e->active_graph->graph_id : 0,
        .node_count = acount(e->selected),
        .pan_x = e->pan.x,
        .pan_y = e->pan.y,
        .zoom = e->zoom,
        .fit = e->fit_mode,
        .entry_index = e->entry_index,
        .caller_count = acount(e->graph_callers),
    };
    if (location.node_count) {
        location.nodes = xmalloc((size_t)location.node_count * sizeof *location.nodes);
        for (int i = 0; i < location.node_count; i++) {
            location.nodes[i] = e->selected[i]->id;
        }
    }
    assert(location.caller_count <= EDITOR_LOCATION_CALLER_LIMIT);
    for (int i = 0; i < location.caller_count; i++) {
        const ed_graph_caller *caller = &e->graph_callers[i];
        location.callers[i] = (editor_caller_location){
            .graph_id = caller->view.graph_id,
            .node_id = caller->node_id,
            .pan_x = caller->view.pan.x,
            .pan_y = caller->view.pan.y,
            .zoom = caller->view.zoom,
            .entry_index = caller->view.entry_index,
            .fit = caller->view.fit_mode,
        };
    }
    return location;
}

static void ed_restore_callers(editor *e, const editor_location *location)
{
    assert(location->caller_count >= 0 && location->caller_count <= EDITOR_LOCATION_CALLER_LIMIT);
    int first = location->caller_count;
    uint64_t target_id = location->graph_id;
    for (int i = location->caller_count - 1; i >= 0; i--) {
        const editor_caller_location *caller = &location->callers[i];
        graph *parent = behavior_asset_find_graph(e->asset, caller->graph_id);
        node *call = parent ? graph_node_by_id(parent, caller->node_id) : NULL;
        if (!ed_is_graph_call(call) || call->reference_id != target_id) {
            break;
        }
        first = i;
        target_id = caller->graph_id;
    }
    aclear(e->graph_callers);
    for (int i = first; i < location->caller_count; i++) {
        const editor_caller_location *caller = &location->callers[i];
        ed_graph_caller restored = {
            .view = {
                .graph_id = caller->graph_id,
                .fit_mode = caller->fit,
                .pan = { caller->pan_x, caller->pan_y },
                .zoom = caller->zoom,
                .view_initialized = 1,
                .entry_index = caller->entry_index,
            },
            .node_id = caller->node_id,
        };
        apush(e->graph_callers, restored);
    }
}

int editor_restore_location(editor *e, const editor_location *location)
{
    graph *target = behavior_asset_find_graph(e->asset, location->graph_id);
    if (!target) {
        return -1;
    }
    for (int i = 0; i < location->node_count; i++) {
        if (!graph_node_by_id(target, location->nodes[i])) {
            return -1;
        }
    }
    if (editor_open_graph(e, location->graph_id) != 0) {
        return -1;
    }
    ed_restore_callers(e, location);
    ed_clear_selection(e);
    e->selected_variable = NULL;
    e->front_node = NULL;
    for (int i = 0; i < location->node_count; i++) {
        ed_add_selected(e, graph_node_by_id(target, location->nodes[i]));
    }
    e->pan = (ImVec2){ location->pan_x, location->pan_y };
    e->zoom = location->zoom;
    e->fit_mode = location->fit;
    e->entry_index = location->entry_index;
    e->view_initialized = 1;
    e->arrange_pending = false;
    e->fit_resize_pending = false;
    e->fit_restore_pending = false;
    e->camera_moving = false;
    ed_save_graph_view(e);
    return 0;
}

static yyjson_mut_val *checkpoint_strings(yyjson_mut_doc *doc, char **strings)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int i = 0; i < acount(strings); i++) {
        yyjson_mut_arr_add_strcpy(doc, array, strings[i]);
    }
    return array;
}

static yyjson_mut_val *checkpoint_view(yyjson_mut_doc *doc, const ed_graph_view *view);

static yyjson_mut_val *checkpoint_document(yyjson_mut_doc *doc, const ed_document_history *history)
{
    yyjson_mut_val *value = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, value, "key", history->key ? history->key : "");
    yyjson_mut_obj_add_strcpy(doc, value, "json", history->json);
    yyjson_mut_obj_add_strcpy(
        doc,
        value,
        "saved",
        history->saved ? history->saved : history->json
    );
    yyjson_mut_obj_add_uint(doc, value, "owner_identity", history->owner_identity);
    yyjson_mut_obj_add_val(doc, value, "undo", checkpoint_strings(doc, history->undo));
    yyjson_mut_obj_add_val(doc, value, "redo", checkpoint_strings(doc, history->redo));
    yyjson_mut_obj_add_uint(doc, value, "variable", history->variable_id);
    yyjson_mut_val *links = yyjson_mut_obj_add_arr(doc, value, "links");
    for (int i = 0; i < acount(history->links); i++) {
        const ed_link_ref *link = &history->links[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, links);
        yyjson_mut_obj_add_uint(doc, item, "from", link->from_id);
        yyjson_mut_obj_add_uint(doc, item, "output", link->from_pin_id);
        yyjson_mut_obj_add_uint(doc, item, "to", link->to_id);
        yyjson_mut_obj_add_uint(doc, item, "input", link->to_pin_id);
    }
    yyjson_mut_val *views = yyjson_mut_obj_add_arr(doc, value, "views");
    for (int i = 0; i < acount(history->views); i++) {
        yyjson_mut_arr_append(views, checkpoint_view(doc, &history->views[i]));
    }
    const editor_location *location = &history->location;
    ed_graph_view view = { .graph_id = location->graph_id,
                           .pan = { location->pan_x, location->pan_y },
                           .zoom = location->zoom,
                           .entry_index = location->entry_index,
                           .fit_mode = location->fit,
                           .view_initialized = true };
    yyjson_mut_val *position = checkpoint_view(doc, &view);
    yyjson_mut_obj_add_val(doc, value, "location", position);
    yyjson_mut_val *selection = yyjson_mut_obj_add_arr(doc, position, "selection");
    for (int i = 0; i < location->node_count; i++) {
        yyjson_mut_arr_add_uint(doc, selection, location->nodes[i]);
    }
    yyjson_mut_val *callers = yyjson_mut_obj_add_arr(doc, position, "callers");
    for (int i = 0; i < location->caller_count; i++) {
        const editor_caller_location *caller = &location->callers[i];
        view = (ed_graph_view){ .graph_id = caller->graph_id,
                                .pan = { caller->pan_x, caller->pan_y },
                                .zoom = caller->zoom,
                                .entry_index = caller->entry_index,
                                .fit_mode = caller->fit,
                                .view_initialized = true };
        yyjson_mut_val *item = checkpoint_view(doc, &view);
        yyjson_mut_obj_add_uint(doc, item, "node", caller->node_id);
        yyjson_mut_arr_append(callers, item);
    }
    return value;
}

static yyjson_mut_val *checkpoint_view(yyjson_mut_doc *doc, const ed_graph_view *view)
{
    yyjson_mut_val *value = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, value, "graph", view->graph_id);
    yyjson_mut_obj_add_real(doc, value, "x", view->pan.x);
    yyjson_mut_obj_add_real(doc, value, "y", view->pan.y);
    yyjson_mut_obj_add_real(doc, value, "zoom", view->zoom);
    yyjson_mut_obj_add_int(doc, value, "entry", view->entry_index);
    yyjson_mut_obj_add_bool(doc, value, "fit", view->fit_mode);
    yyjson_mut_obj_add_bool(doc, value, "initialized", view->view_initialized);
    return value;
}

char *editor_checkpoint(editor *e)
{
    ed_commit_snapshot(e);
    e->dragging = e->selecting = e->panning = e->linking = 0;
    e->edit_history_open = e->details_history_open = 0;
    ed_save_graph_view(e);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    char *json = behavior_asset_save_json(e->asset);
    ed_document_history current = { .key = e->document_key,
                                    .json = json,
                                    .saved = e->saved_snapshot,
                                    .owner_identity = e->asset ? e->asset->identity : 0,
                                    .undo = e->undo,
                                    .redo = e->redo,
                                    .views = e->graph_views,
                                    .location = editor_capture_location(e) };
    current.variable_id = e->selected_variable ? e->selected_variable->id : 0;
    current.links = e->selected_links;
    yyjson_mut_obj_add_val(doc, root, "active", checkpoint_document(doc, &current));
    free(json);
    free(current.location.nodes);
    yyjson_mut_val *documents = yyjson_mut_obj_add_arr(doc, root, "documents");
    for (int i = 0; i < acount(e->documents); i++) {
        yyjson_mut_arr_append(documents, checkpoint_document(doc, &e->documents[i]));
    }
    yyjson_mut_obj_add_uint(doc, root, "graph", e->active_graph->graph_id);
    yyjson_mut_val *views = yyjson_mut_obj_add_arr(doc, root, "views");
    for (int i = 0; i < acount(e->graph_views); i++) {
        if (behavior_asset_find_graph(e->asset, e->graph_views[i].graph_id)) {
            yyjson_mut_arr_append(views, checkpoint_view(doc, &e->graph_views[i]));
        }
    }
    yyjson_mut_val *selection = yyjson_mut_obj_add_arr(doc, root, "selection");
    for (int i = 0; i < acount(e->selected); i++) {
        yyjson_mut_arr_add_uint(doc, selection, e->selected[i]->id);
    }
    yyjson_mut_val *callers = yyjson_mut_obj_add_arr(doc, root, "callers");
    for (int i = 0; i < acount(e->graph_callers); i++) {
        yyjson_mut_val *caller = checkpoint_view(doc, &e->graph_callers[i].view);
        yyjson_mut_obj_add_uint(doc, caller, "node", e->graph_callers[i].node_id);
        yyjson_mut_arr_append(callers, caller);
    }
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

static bool checkpoint_asset_valid(const char *json)
{
    behavior_asset *asset = json ? behavior_asset_load_json(json) : NULL;
    bool valid = asset != NULL;
    behavior_asset_free(asset);
    return valid;
}

static bool checkpoint_read_strings(yyjson_val *value, char ***strings)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) > ED_HIST_MAX) {
        return false;
    }
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(value, i, count, item)
    {
        const char *text = yyjson_get_str(item);
        if (!checkpoint_asset_valid(text)) {
            return false;
        }
        apush(*strings, xstrdup(text));
    }
    return true;
}

static void checkpoint_free_document(ed_document_history *history)
{
    free(history->key);
    free(history->json);
    free(history->saved);
    ed_free_string_stack(&history->undo);
    ed_free_string_stack(&history->redo);
    afree(history->views);
    afree(history->links);
    free(history->location.nodes);
}

static bool checkpoint_read_view(yyjson_val *value, ed_graph_view *view)
{
    yyjson_val *graph = yyjson_obj_get(value, "graph");
    yyjson_val *x = yyjson_obj_get(value, "x"), *y = yyjson_obj_get(value, "y");
    yyjson_val *zoom = yyjson_obj_get(value, "zoom");
    yyjson_val *entry = yyjson_obj_get(value, "entry");
    yyjson_val *fit = yyjson_obj_get(value, "fit");
    yyjson_val *initialized = yyjson_obj_get(value, "initialized");
    if (!yyjson_is_uint(graph) || !yyjson_is_num(x) || !yyjson_is_num(y) ||
        !yyjson_is_num(zoom) || !yyjson_is_int(entry) || !yyjson_is_bool(fit) ||
        !yyjson_is_bool(initialized)) {
        return false;
    }
    *view = (ed_graph_view){
        .graph_id = yyjson_get_uint(graph),
        .pan = { (float)yyjson_get_num(x), (float)yyjson_get_num(y) },
        .zoom = (float)yyjson_get_num(zoom),
        .entry_index = yyjson_get_int(entry),
        .fit_mode = yyjson_get_bool(fit),
        .view_initialized = yyjson_get_bool(initialized),
    };
    return isfinite(view->pan.x) && isfinite(view->pan.y) &&
           isfinite(view->zoom) && view->zoom > 0;
}

static bool checkpoint_selection_valid(const ed_document_history *history)
{
    behavior_asset *asset = behavior_asset_load_json(history->json);
    graph *g = behavior_asset_find_graph(asset, history->location.graph_id);
    bool valid = g && (!history->variable_id ||
                       behavior_asset_find_variable_by_id(asset, history->variable_id));
    for (int i = 0; valid && i < history->location.node_count; i++) {
        valid = graph_node_by_id(g, history->location.nodes[i]) != NULL;
    }
    for (int i = 0; valid && i < acount(history->links); i++) {
        const ed_link_ref *link = &history->links[i];
        bool found = false;
        for (int j = 0; j < acount(g->edges); j++) {
            const edge *wire = &g->edges[j];
            found |= wire->from_id == link->from_id &&
                     wire->from_pin_id == link->from_pin_id &&
                     wire->to_id == link->to_id && wire->to_pin_id == link->to_pin_id;
        }
        valid = found;
    }
    behavior_asset_free(asset);
    return valid;
}

static bool checkpoint_read_document(yyjson_val *value, ed_document_history *history)
{
    const char *key = yyjson_get_str(yyjson_obj_get(value, "key"));
    const char *json = yyjson_get_str(yyjson_obj_get(value, "json"));
    const char *saved = yyjson_get_str(yyjson_obj_get(value, "saved"));
    if (!key || !checkpoint_asset_valid(json) || !checkpoint_asset_valid(saved)) {
        return false;
    }
    history->key = xstrdup(key);
    history->json = xstrdup(json);
    history->saved = xstrdup(saved);
    yyjson_val *owner_identity = yyjson_obj_get(value, "owner_identity");
    if (owner_identity &&
        (!yyjson_is_uint(owner_identity) || !yyjson_get_uint(owner_identity))) {
        return false;
    }
    history->owner_identity = owner_identity ? yyjson_get_uint(owner_identity) : 0;
    if (!checkpoint_read_strings(yyjson_obj_get(value, "undo"), &history->undo) ||
        !checkpoint_read_strings(yyjson_obj_get(value, "redo"), &history->redo)) {
        return false;
    }
    yyjson_val *variable = yyjson_obj_get(value, "variable");
    yyjson_val *links = yyjson_obj_get(value, "links");
    if (!yyjson_is_uint(variable) || !yyjson_is_arr(links)) {
        return false;
    }
    history->variable_id = yyjson_get_uint(variable);
    size_t link_index, link_count;
    yyjson_val *link;
    yyjson_arr_foreach(links, link_index, link_count, link)
    {
        yyjson_val *from = yyjson_obj_get(link, "from");
        yyjson_val *output = yyjson_obj_get(link, "output");
        yyjson_val *to = yyjson_obj_get(link, "to");
        yyjson_val *input = yyjson_obj_get(link, "input");
        if (!yyjson_is_uint(from) || !yyjson_is_uint(output) ||
            !yyjson_is_uint(to) || !yyjson_is_uint(input)) {
            return false;
        }
        apush(
            history->links,
            ((ed_link_ref){ yyjson_get_uint(from),
                            yyjson_get_uint(output),
                            yyjson_get_uint(to),
                            yyjson_get_uint(input) })
        );
    }
    yyjson_val *views = yyjson_obj_get(value, "views");
    yyjson_val *position = yyjson_obj_get(value, "location");
    yyjson_val *selection = yyjson_obj_get(position, "selection");
    yyjson_val *callers = yyjson_obj_get(position, "callers");
    ed_graph_view view;
    if (!yyjson_is_arr(views) || !yyjson_is_arr(selection) || !yyjson_is_arr(callers) ||
        yyjson_arr_size(callers) > EDITOR_LOCATION_CALLER_LIMIT ||
        !checkpoint_read_view(position, &view)) {
        return false;
    }
    history->location = (editor_location){ .graph_id = view.graph_id,
                                           .pan_x = view.pan.x,
                                           .pan_y = view.pan.y,
                                           .zoom = view.zoom,
                                           .fit = view.fit_mode,
                                           .entry_index = view.entry_index };
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(views, i, count, item)
    {
        if (!checkpoint_read_view(item, &view)) {
            return false;
        }
        apush(history->views, view);
    }
    history->location.node_count = (int)yyjson_arr_size(selection);
    history->location.nodes = xmalloc(yyjson_arr_size(selection) * sizeof(uint64_t));
    yyjson_arr_foreach(selection, i, count, item)
    {
        if (!yyjson_is_uint(item)) {
            return false;
        }
        history->location.nodes[i] = yyjson_get_uint(item);
    }
    yyjson_arr_foreach(callers, i, count, item)
    {
        if (!checkpoint_read_view(item, &view) ||
            !yyjson_is_uint(yyjson_obj_get(item, "node"))) {
            return false;
        }
        history->location.callers[history->location.caller_count++] = (editor_caller_location){
            .graph_id = view.graph_id,
            .node_id = yyjson_get_uint(yyjson_obj_get(item, "node")),
            .pan_x = view.pan.x,
            .pan_y = view.pan.y,
            .zoom = view.zoom,
            .fit = view.fit_mode,
            .entry_index = view.entry_index,
        };
    }
    return checkpoint_selection_valid(history);
}

bool editor_restore_checkpoint(editor *e, const char *json)
{
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ed_document_history current = { 0 }, *documents = NULL;
    ed_graph_view *views = NULL;
    editor_location location = { 0 };
    behavior_asset *asset = NULL;
    bool ok = false;
    yyjson_val *stored = yyjson_obj_get(root, "documents");
    yyjson_val *view_array = yyjson_obj_get(root, "views");
    yyjson_val *selection = yyjson_obj_get(root, "selection");
    yyjson_val *callers = yyjson_obj_get(root, "callers");
    yyjson_val *graph_id = yyjson_obj_get(root, "graph");
    if (yyjson_get_int(yyjson_obj_get(root, "version")) != 1 ||
        !yyjson_is_arr(stored) || !yyjson_is_arr(view_array) ||
        !yyjson_is_arr(selection) || !yyjson_is_arr(callers) ||
        yyjson_arr_size(callers) > EDITOR_LOCATION_CALLER_LIMIT ||
        !yyjson_is_uint(graph_id) ||
        !checkpoint_read_document(yyjson_obj_get(root, "active"), &current)) {
        goto done;
    }
    asset = behavior_asset_load_json(current.json);
    if (asset && current.owner_identity) {
        asset->identity = current.owner_identity;
    }
    location.graph_id = yyjson_get_uint(graph_id);
    graph *active = behavior_asset_find_graph(asset, location.graph_id);
    if (!active) {
        goto done;
    }
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(stored, i, count, item)
    {
        ed_document_history history = { 0 };
        bool valid = checkpoint_read_document(item, &history);
        apush(documents, history);
        if (!valid || !strcmp(history.key, current.key)) {
            goto done;
        }
        for (int j = 0; j < acount(documents) - 1; j++) {
            if (!strcmp(documents[j].key, history.key)) {
                goto done;
            }
        }
    }
    bool found_view = false;
    yyjson_arr_foreach(view_array, i, count, item)
    {
        ed_graph_view view;
        if (!checkpoint_read_view(item, &view) ||
            !behavior_asset_find_graph(asset, view.graph_id)) {
            goto done;
        }
        apush(views, view);
        if (view.graph_id == location.graph_id) {
            found_view = true;
            location.pan_x = view.pan.x;
            location.pan_y = view.pan.y;
            location.zoom = view.zoom;
            location.fit = view.fit_mode;
            location.entry_index = view.entry_index;
        }
    }
    if (!found_view) {
        goto done;
    }
    yyjson_arr_foreach(selection, i, count, item)
    {
        if (!yyjson_is_uint(item) || !graph_node_by_id(active, yyjson_get_uint(item))) {
            goto done;
        }
        apush(location.nodes, yyjson_get_uint(item));
    }
    location.node_count = acount(location.nodes);
    yyjson_arr_foreach(callers, i, count, item)
    {
        ed_graph_view view;
        yyjson_val *node_id = yyjson_obj_get(item, "node");
        if (!checkpoint_read_view(item, &view) || !yyjson_is_uint(node_id)) {
            goto done;
        }
        location.callers[location.caller_count++] = (editor_caller_location){
            .graph_id = view.graph_id,
            .node_id = yyjson_get_uint(node_id),
            .pan_x = view.pan.x,
            .pan_y = view.pan.y,
            .zoom = view.zoom,
            .fit = view.fit_mode,
            .entry_index = view.entry_index,
        };
    }
    if (editor_open_behavior_asset(e, asset, location.graph_id) != 0) {
        goto done;
    }
    for (int j = 0; j < acount(e->documents); j++) {
        checkpoint_free_document(&e->documents[j]);
    }
    afree(e->documents);
    e->documents = documents;
    documents = NULL;
    free(e->document_key);
    free(e->saved_snapshot);
    ed_free_string_stack(&e->undo);
    ed_free_string_stack(&e->redo);
    e->document_key = current.key;
    e->saved_snapshot = current.saved;
    e->undo = current.undo;
    e->redo = current.redo;
    uint64_t variable_id = current.variable_id;
    ed_link_ref *links = current.links;
    free(current.json);
    afree(current.views);
    free(current.location.nodes);
    current = (ed_document_history){ 0 };
    afree(e->graph_views);
    e->graph_views = views;
    views = NULL;
    ok = editor_restore_location(e, &location) == 0;
    e->selected_variable = behavior_asset_find_variable_by_id(e->asset, variable_id);
    for (int i = 0; i < acount(links); i++) {
        apush(e->selected_links, links[i]);
    }
    afree(links);
done:
    checkpoint_free_document(&current);
    for (int j = 0; j < acount(documents); j++) {
        checkpoint_free_document(&documents[j]);
    }
    afree(documents);
    afree(views);
    afree(location.nodes);
    behavior_asset_free(asset);
    yyjson_doc_free(doc);
    return ok;
}
