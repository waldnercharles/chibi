/* Find References (Unreal): every node across the project that calls, raises,
 * handles, spawns, reads or writes a function, variable, event or asset, from
 * the asset index, listed in Output's Find view. A row opens its asset and
 * reveals its node. */
#include "editor/studio/studio_graph_internal.h"
#include "graph/document/events.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <stdio.h>
#include <string.h>

static const char *use_label(asset_index_reference_kind kind)
{
    switch (kind) {
        case ASSET_INDEX_GRAPH_CALL: return "Call";
        case ASSET_INDEX_PROTOTYPE_SPAWN: return "Spawn";
        case ASSET_INDEX_EVENT_CONSUMER: return "Handle";
        case ASSET_INDEX_EVENT_EMITTER: return "Send";
        case ASSET_INDEX_BEHAVIOR_ATTACHMENT: return "Attach";
        case ASSET_INDEX_COMPONENT_BINDING: return "Bind";
        case ASSET_INDEX_VARIABLE_READ: return "Read";
        case ASSET_INDEX_VARIABLE_WRITE: return "Write";
        case ASSET_INDEX_REFERENCE: return "Use";
    }
    return "Use";
}

/* A node reference names its graph; an asset reference, what holds it. */
static studio_reference row_of(const asset_index *index, const asset_index_reference *use)
{
    const asset_index_asset *source = asset_index_asset_at(index, use->source);
    studio_reference row = { .kind = (int)source->kind,
                             .graph_id = use->graph_id,
                             .node_id = use->node_id,
                             .object = !use->node_id && use->actor,
                             .use = use_label(use->kind) };
    snprintf(row.asset, sizeof row.asset, "%s", source->name);
    const char *place = use->node_id
                          ? (use->detail && use->detail[0] ? use->detail : "Event Graph")
                      : use->actor ? use->actor
                                   : use->detail;
    snprintf(row.place, sizeof row.place, "%s", place ? place : "");
    return row;
}

static void find(studio *s, const char *title, asset_index_target target)
{
    const asset_index *index = app_scene_authoring_asset_index(s->scene);
    const asset_index_reference **uses = NULL;
    asset_index_find_uses(index, target, &uses);
    asetlen(s->references, 0);
    for (int i = 0; i < acount(uses); i++) {
        apush(s->references, row_of(index, uses[i]));
    }
    afree(uses);
    snprintf(s->references_title, sizeof s->references_title, "%s", title);
    studio_output_find(s);
}

/* The index position of the open graph document, fresh with its edits. */
static int document_asset(studio *s)
{
    app_scene_authoring *scene = s->scene;
    app_scene_authoring_refresh_asset_index(scene);
    const asset_index *index = app_scene_authoring_asset_index(scene);
    editor *e = sg_graph_editor(s);
    if (scene->graph_document_kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        return asset_index_find_name(index, ASSET_INDEX_SYSTEM, scene->graph_document_id);
    }
    return asset_index_find_name(
        index,
        asset_index_domain_kind(e->asset->domain),
        scene->graph_document_id
    );
}

/* A target the index lacks lists no references rather than nothing at all. */
void studio_find_document_references(studio *s)
{
    int asset = document_asset(s);
    find(s, s->scene->graph_document_id, (asset_index_target){ asset, ASSET_INDEX_SYMBOL_ASSET, 0 });
}

void studio_find_asset_references(studio *s, int asset)
{
    app_scene_authoring_refresh_asset_index(s->scene);
    const asset_index_asset
        *found = asset_index_asset_at(app_scene_authoring_asset_index(s->scene), asset);
    if (found) {
        find(s, found->name, (asset_index_target){ asset, ASSET_INDEX_SYMBOL_ASSET, 0 });
    }
}

void studio_find_graph_references(studio *s, const graph *gr)
{
    int asset = document_asset(s);
    find(s, gr->name, (asset_index_target){ asset, ASSET_INDEX_SYMBOL_FUNCTION, gr->graph_id });
}

void studio_find_variable_references(studio *s, uint64_t variable_id)
{
    const behavior_variable
        *v = behavior_asset_find_variable_by_id(sg_graph_editor(s)->asset, variable_id);
    int asset = document_asset(s);
    find(s, v ? v->name : "Variable", (asset_index_target){ asset, ASSET_INDEX_SYMBOL_VARIABLE, variable_id });
}

static bool graph_call(const node *n)
{
    return n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
           n->desc == &NODE_CallMacro;
}

static bool event_node(const node *n)
{
    const char *name = n->desc->name;
    return n->reference_name && !graph_event_host_name(n->reference_name) &&
           (!strcmp(name, "Event") || !strcmp(name, "Call Event") ||
            !strcmp(name, "Send Event"));
}

static const char *spawned_prototype(const node *n)
{
    if (strcmp(n->desc->name, "Spawn Prototype")) {
        return NULL;
    }
    for (int p = 0; p < n->input_count; p++) {
        const pin_spec *pin = &n->input_pins[p];
        if (!strcmp(pin->name, "Prototype") && pin->default_value.str &&
            pin->default_value.str[0]) {
            return pin->default_value.str;
        }
    }
    return NULL;
}

bool studio_node_referable(const node *n)
{
    return graph_call(n) || n->desc == &NODE_GetVariable ||
           n->desc == &NODE_SetVariable || event_node(n) || spawned_prototype(n);
}

/* What a node references: the function it calls, the variable it reads or
   writes, the event it raises or handles, or the prototype it spawns. */
void studio_find_node_references(studio *s, const node *n)
{
    int document = document_asset(s);
    const asset_index *index = app_scene_authoring_asset_index(s->scene);
    const char *title = n->reference_name;
    asset_index_target target = { -1, ASSET_INDEX_SYMBOL_ASSET, 0 };
    if (graph_call(n)) {
        bool library = n->reference_asset && n->reference_asset[0];
        target = (asset_index_target){
            library ? asset_index_find_name(index, ASSET_INDEX_LIBRARY, n->reference_asset)
                    : document,
            ASSET_INDEX_SYMBOL_FUNCTION,
            n->reference_id
        };
    } else if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
        target = (asset_index_target){ document, ASSET_INDEX_SYMBOL_VARIABLE, n->reference_id };
    } else if (event_node(n)) {
        target.asset = asset_index_find_name(index, ASSET_INDEX_EVENT, n->reference_name);
    } else if (spawned_prototype(n)) {
        title = spawned_prototype(n);
        target.asset = asset_index_find_name(index, ASSET_INDEX_PROTOTYPE, title);
    }
    find(s, title ? title : n->desc->name, target);
}

void studio_find_clear(studio *s)
{
    asetlen(s->references, 0);
    s->references_title[0] = '\0';
}

static bool matches(const studio_reference *row, const char *filter)
{
    return studio_matches(filter, row->asset) ||
           studio_matches(filter, row->place) || studio_matches(filter, row->use);
}

/* The open document stays open, keeping its history; another opens first.
   A node reference reveals its node, and a scene's its object. */
static void open_reference(studio *s, const studio_reference *row)
{
    app_scene_authoring *scene = s->scene;
    bool open = scene->graph_document_id &&
                !strcmp(scene->graph_document_id, row->asset) &&
                (row->kind == ASSET_INDEX_SYSTEM) ==
                    (scene->graph_document_kind == APP_EDITOR_DOCUMENT_SYSTEM);
    if (!open) {
        int asset = asset_index_find_name(
            app_scene_authoring_asset_index(scene),
            (asset_index_kind)row->kind,
            row->asset
        );
        finish_transform_edit(scene, false);
        if (asset < 0 || app_scene_authoring_open_asset(scene, asset) != 0) {
            studio_report(s, "Could not open the reference");
            return;
        }
    }
    if (row->node_id) {
        studio_reveal_node(s, row->graph_id, row->node_id);
    }
    for (int i = 0; row->object && i < acount(scene->actors); i++) {
        if (scene->actors[i].name && !strcmp(scene->actors[i].name, row->place)) {
            select_instance(scene, i);
            s->outliner_reveal = true;
        }
    }
}

void studio_find_view(studio *s, float row_height)
{
    kit *k = s->kit;
    int *shown = NULL;
    for (int i = 0; i < acount(s->references); i++) {
        if (matches(&s->references[i], s->output_filter)) {
            apush(shown, i);
        }
    }
    if (s->references_title[0]) {
        char count[16];
        snprintf(count, sizeof count, "%d", acount(s->references));
        kit_push(k, "head", NULL, "find-head");
        kit_icon(k, "icon", NULL, KIT_ICON_REFERENCES);
        kit_label(k, "title", "find-title", s->references_title);
        kit_label(k, "count", "find-count", count);
        kit_pop(k);
    }
    kit_list_desc list = { .count = acount(shown),
                           .row_height = row_height,
                           .reveal = -1 };
    int first, last;
    kit_list_begin(k, "references", "output-list find-list", &list, &first, &last);
    for (int r = first; r <= last && r < acount(shown); r++) {
        const studio_reference *ref = &s->references[shown[r]];
        char key[16], label[320];
        snprintf(key, sizeof key, "f%d", shown[r]);
        snprintf(label, sizeof label, ref->place[0] ? "%s \xe2\x80\xba %s" : "%s", ref->asset, ref->place);
        kit_row_desc d = { .label = label,
                           .icon = studio_asset_kind_icon(ref->kind),
                           .twisty = -1,
                           .meta = ref->use };
        if (kit_row_begin(k, key, &d) & KIT_ROW_CLICK) {
            open_reference(s, ref);
        }
        kit_row_end(k);
    }
    kit_list_end(k);
    if (!acount(shown)) {
        kit_label(k, "empty", "output-empty", acount(s->references) ? "No matches" : "No references");
    }
    afree(shown);
}
