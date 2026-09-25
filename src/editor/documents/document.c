#include "../third_party/yyjson/yyjson.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static ed_location capture_location(const editor *e);
static int restore_location(editor *e, const ed_location *location);

static void ed_keep_document(editor *e)
{
    if (!e->document_key) {
        return;
    }
    ed_commit_snapshot(e);
    ed_document_history history = {
        .key = e->document_key,
        .json = behavior_asset_save_json(e->asset),
        .saved = e->saved_snapshot,
        .owner_identity = e->asset ? e->asset->identity : 0,
        .undo = e->undo,
        .redo = e->redo,
        .location = capture_location(e),
    };
    for (int i = 0; i < acount(e->selected_links); i++) {
        apush(history.links, e->selected_links[i]);
    }
    apush(e->documents, history);
    e->document_key = NULL;
    e->saved_snapshot = NULL;
    e->undo = NULL;
    e->redo = NULL;
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
        if (history.location.graph_id == graph_id) {
            restore_location(e, &history.location);
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
        afree(history->links);
        free(history->location.nodes);
        adel(e->documents, i);
        break;
    }
    e->document_key = copy;
    return 0;
}

int editor_reload_behavior_asset_document(editor *e, const behavior_asset *source, const char *key)
{
    if (!e || !source || !key) {
        return -1;
    }
    if (e->document_key && !strcmp(e->document_key, key)) {
        const graph *graph = editor_active_graph(e);
        return graph && behavior_asset_find_graph(source, graph->graph_id)
                 ? editor_reload_behavior_asset(e, source, graph->graph_id, key)
                 : -1;
    }
    for (int i = 0; i < acount(e->documents); i++) {
        ed_document_history *history = &e->documents[i];
        if (strcmp(history->key, key) != 0) {
            continue;
        }
        char *json = behavior_asset_save_json(source);
        if (!json) {
            return -1;
        }
        free(history->json);
        history->json = json;
        free(history->saved);
        history->saved = xstrdup(json);
        ed_free_string_stack(&history->undo);
        ed_free_string_stack(&history->redo);
        free(history->location.nodes);
        history->location = (ed_location){ 0 };
        afree(history->links);
        history->owner_identity = source->identity;
        return 0;
    }
    return 0;
}

static ed_location capture_location(const editor *e)
{
    ed_location location = {
        .graph_id = e->active_graph->graph_id,
        .node_count = acount(e->selected),
        .caller_count = acount(e->graph_callers),
    };
    if (location.node_count) {
        location.nodes = xmalloc((size_t)location.node_count * sizeof *location.nodes);
        for (int i = 0; i < location.node_count; i++) {
            location.nodes[i] = e->selected[i]->id;
        }
    }
    assert(location.caller_count <= ED_CALLER_LIMIT);
    memcpy(
        location.callers,
        e->graph_callers,
        (size_t)location.caller_count * sizeof *location.callers
    );
    return location;
}

/* Keeps the innermost unbroken chain of calls that still leads to the graph. */
static void restore_callers(editor *e, const ed_location *location)
{
    assert(location->caller_count >= 0 && location->caller_count <= ED_CALLER_LIMIT);
    int first = location->caller_count;
    uint64_t target_id = location->graph_id;
    for (int i = location->caller_count - 1; i >= 0; i--) {
        const ed_graph_caller *caller = &location->callers[i];
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
        apush(e->graph_callers, location->callers[i]);
    }
}

static int restore_location(editor *e, const ed_location *location)
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
    restore_callers(e, location);
    ed_clear_selection(e);
    e->front_node = NULL;
    for (int i = 0; i < location->node_count; i++) {
        ed_add_selected(e, graph_node_by_id(target, location->nodes[i]));
    }
    e->arrange_pending = false;
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

static yyjson_mut_val *checkpoint_location(yyjson_mut_doc *doc, const ed_location *location)
{
    yyjson_mut_val *value = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, value, "graph", location->graph_id);
    yyjson_mut_val *selection = yyjson_mut_obj_add_arr(doc, value, "selection");
    for (int i = 0; i < location->node_count; i++) {
        yyjson_mut_arr_add_uint(doc, selection, location->nodes[i]);
    }
    yyjson_mut_val *callers = yyjson_mut_obj_add_arr(doc, value, "callers");
    for (int i = 0; i < location->caller_count; i++) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, callers);
        yyjson_mut_obj_add_uint(doc, item, "graph", location->callers[i].graph_id);
        yyjson_mut_obj_add_uint(doc, item, "node", location->callers[i].node_id);
    }
    return value;
}

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
    yyjson_mut_val *links = yyjson_mut_obj_add_arr(doc, value, "links");
    for (int i = 0; i < acount(history->links); i++) {
        const ed_link_ref *link = &history->links[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, links);
        yyjson_mut_obj_add_uint(doc, item, "from", link->from_id);
        yyjson_mut_obj_add_uint(doc, item, "output", link->from_pin_id);
        yyjson_mut_obj_add_uint(doc, item, "to", link->to_id);
        yyjson_mut_obj_add_uint(doc, item, "input", link->to_pin_id);
    }
    yyjson_mut_obj_add_val(
        doc,
        value,
        "location",
        checkpoint_location(doc, &history->location)
    );
    return value;
}

char *editor_checkpoint(editor *e)
{
    ed_commit_snapshot(e);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    char *json = behavior_asset_save_json(e->asset);
    ed_document_history current = { .key = e->document_key,
                                    .json = json,
                                    .saved = e->saved_snapshot,
                                    .owner_identity = e->asset->identity,
                                    .undo = e->undo,
                                    .redo = e->redo,
                                    .location = capture_location(e) };
    current.links = e->selected_links;
    yyjson_mut_obj_add_val(doc, root, "active", checkpoint_document(doc, &current));
    free(json);
    free(current.location.nodes);
    yyjson_mut_val *documents = yyjson_mut_obj_add_arr(doc, root, "documents");
    for (int i = 0; i < acount(e->documents); i++) {
        yyjson_mut_arr_append(documents, checkpoint_document(doc, &e->documents[i]));
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
    afree(history->links);
    free(history->location.nodes);
}

static bool checkpoint_read_location(yyjson_val *value, ed_location *location)
{
    yyjson_val *graph = yyjson_obj_get(value, "graph");
    yyjson_val *selection = yyjson_obj_get(value, "selection");
    yyjson_val *callers = yyjson_obj_get(value, "callers");
    if (!yyjson_is_uint(graph) || !yyjson_is_arr(selection) ||
        !yyjson_is_arr(callers) || yyjson_arr_size(callers) > ED_CALLER_LIMIT) {
        return false;
    }
    location->graph_id = yyjson_get_uint(graph);
    location->node_count = (int)yyjson_arr_size(selection);
    location->nodes = xmalloc(yyjson_arr_size(selection) * sizeof(uint64_t));
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(selection, i, count, item)
    {
        if (!yyjson_is_uint(item)) {
            return false;
        }
        location->nodes[i] = yyjson_get_uint(item);
    }
    yyjson_arr_foreach(callers, i, count, item)
    {
        yyjson_val *caller_graph = yyjson_obj_get(item, "graph");
        yyjson_val *node = yyjson_obj_get(item, "node");
        if (!yyjson_is_uint(caller_graph) || !yyjson_is_uint(node)) {
            return false;
        }
        location->callers[location->caller_count++] = (ed_graph_caller){
            yyjson_get_uint(caller_graph),
            yyjson_get_uint(node),
        };
    }
    return true;
}

static bool checkpoint_selection_valid(const ed_document_history *history)
{
    behavior_asset *asset = behavior_asset_load_json(history->json);
    graph *g = behavior_asset_find_graph(asset, history->location.graph_id);
    bool valid = g != NULL;
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
    yyjson_val *links = yyjson_obj_get(value, "links");
    if (!yyjson_is_arr(links)) {
        return false;
    }
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
    if (!checkpoint_read_location(yyjson_obj_get(value, "location"), &history->location)) {
        return false;
    }
    return checkpoint_selection_valid(history);
}

bool editor_restore_checkpoint(editor *e, const char *json)
{
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    ed_document_history current = { 0 }, *documents = NULL;
    behavior_asset *asset = NULL;
    bool ok = false;
    yyjson_val *stored = yyjson_obj_get(root, "documents");
    if (yyjson_get_int(yyjson_obj_get(root, "version")) != 1 || !yyjson_is_arr(stored) ||
        !checkpoint_read_document(yyjson_obj_get(root, "active"), &current)) {
        goto done;
    }
    asset = behavior_asset_load_json(current.json);
    if (current.owner_identity) {
        asset->identity = current.owner_identity;
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
    if (editor_open_behavior_asset(e, asset, current.location.graph_id) != 0) {
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
    current.key = current.saved = NULL;
    current.undo = current.redo = NULL;
    ok = restore_location(e, &current.location) == 0;
    for (int j = 0; j < acount(current.links); j++) {
        apush(e->selected_links, current.links[j]);
    }
done:
    checkpoint_free_document(&current);
    for (int j = 0; j < acount(documents); j++) {
        checkpoint_free_document(&documents[j]);
    }
    afree(documents);
    behavior_asset_free(asset);
    yyjson_doc_free(doc);
    return ok;
}
