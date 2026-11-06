#include "graph/model/behavior_asset.h"

#include "graph/model/graph_internal.h"
#include "graph/model/value_copy.h"
#include "support/asset_format.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static atomic_ullong behavior_asset_identity = 1;

const char *behavior_domain_name(behavior_domain domain)
{
    switch (domain) {
        case BEHAVIOR_DOMAIN_MODEL: return "model";
        case BEHAVIOR_DOMAIN_FIELD: return "field";
        case BEHAVIOR_DOMAIN_EVENT: return "behavior";
        case BEHAVIOR_DOMAIN_LIBRARY: return "library";
        case BEHAVIOR_DOMAIN_MATERIAL: return "material";
        case BEHAVIOR_DOMAIN_SHAPE: return "shape";
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION: return "event";
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY: return "shader-library";
    }
    return "invalid";
}

const char *behavior_asset_extension(behavior_domain domain)
{
    switch (domain) {
        case BEHAVIOR_DOMAIN_EVENT:
        case BEHAVIOR_DOMAIN_LIBRARY: return CODE_ASSET_EXTENSION;
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION:
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY:
        case BEHAVIOR_DOMAIN_FIELD:
        case BEHAVIOR_DOMAIN_MODEL:
        case BEHAVIOR_DOMAIN_MATERIAL:
        case BEHAVIOR_DOMAIN_SHAPE: return DECLARATIVE_ASSET_EXTENSION;
    }
    assert(false);
    return NULL;
}

bool behavior_domain_pure(behavior_domain domain)
{
    return domain == BEHAVIOR_DOMAIN_MATERIAL || domain == BEHAVIOR_DOMAIN_SHAPE ||
           domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY ||
           domain == BEHAVIOR_DOMAIN_FIELD || domain == BEHAVIOR_DOMAIN_MODEL;
}

int behavior_asset_variable_name_unique(const behavior_asset *asset, const char *name, uint64_t except_id)
{
    for (int i = 0; i < acount(asset->variables); i++) {
        if (asset->variables[i].id != except_id &&
            strcmp(asset->variables[i].name, name) == 0) {
            return 0;
        }
    }
    return 1;
}

behavior_asset *behavior_asset_new(const char *name)
{
    behavior_asset *asset = (behavior_asset *)xcalloc(1, sizeof *asset);
    asset->identity = atomic_fetch_add_explicit(&behavior_asset_identity, 1, memory_order_relaxed);
    if (!asset->identity) {
        asset->identity = atomic_fetch_add_explicit(&behavior_asset_identity, 1, memory_order_relaxed);
    }
    asset->graph = (graph *)xcalloc(1, sizeof *asset->graph);
    graph_init(asset->graph);
    asset->graph->owner = asset;
    asset->graph->graph_id = 0;
    asset->graph->shared_id_counter = &asset->graph->id_counter;
    apush(asset->graphs, asset->graph);
    asset->name = graph_own_str(asset->graph, name ? name : "", name ? strlen(name) : 0);
    asset->description = graph_own_str(asset->graph, "", 0);
    return asset;
}

behavior_component_parameter *behavior_asset_add_component_parameter(
    behavior_asset *asset,
    const char *name,
    const char *key,
    bool optional
)
{
    assert(asset && name && *name && key && *key && behavior_asset_mutable(asset));
    behavior_component_parameter parameter = {
        graph_own_str(asset->graph, name, strlen(name)),
        graph_own_str(asset->graph, key, strlen(key)),
        optional
    };
    apush(asset->component_parameters, parameter);
    return &asset->component_parameters[acount(asset->component_parameters) - 1];
}

void *behavior_asset_artifact(const behavior_asset *asset)
{
    return asset->artifact;
}
void behavior_asset_set_artifact(behavior_asset *asset, void *artifact, void (*release)(void *))
{
    assert(asset && (!!artifact == !!release));
    if (asset->artifact == artifact) {
        assert(asset->artifact_free == release);
        return;
    }
    if (asset->artifact) {
        asset->artifact_free(asset->artifact);
    }
    asset->artifact = artifact;
    asset->artifact_free = release;
}

void behavior_asset_free(behavior_asset *asset)
{
    if (!asset) {
        return;
    }
    afree(asset->variables);
    afree(asset->component_parameters);
    afree(asset->reactives);
    behavior_asset_set_artifact(asset, NULL, NULL);
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph_free(asset->graphs[i]);
        free(asset->graphs[i]);
    }
    afree(asset->graphs);
    free(asset);
}

graph *behavior_asset_add_graph(behavior_asset *asset, graph_kind kind, const char *name)
{
    if (!asset || !behavior_asset_mutable(asset) || !name || !name[0]) {
        return NULL;
    }
    if (behavior_asset_find_graph_by_name(asset, name)) {
        return NULL;
    }
    graph *g = (graph *)xcalloc(1, sizeof *g);
    graph_init(g);
    g->owner = asset;
    g->kind = kind;
    g->graph_id = graph_alloc_id(asset->graph);
    g->shared_id_counter = &asset->graph->id_counter;
    g->id_counter = asset->graph->id_counter;
    g->name = graph_own_str(g, name, strlen(name));
    g->description = graph_own_str(g, "", 0);
    apush(asset->graphs, g);
    return g;
}

graph *behavior_asset_find_graph(const behavior_asset *asset, uint64_t graph_id)
{
    if (!asset) {
        return NULL;
    }
    if (graph_id == 0) {
        return asset->graph;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (asset->graphs[i]->graph_id == graph_id) {
            return asset->graphs[i];
        }
    }
    return NULL;
}

graph *behavior_asset_find_graph_by_name(const behavior_asset *asset, const char *name)
{
    if (!asset || !name) {
        return NULL;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (asset->graphs[i]->name && strcmp(asset->graphs[i]->name, name) == 0) {
            return asset->graphs[i];
        }
    }
    return NULL;
}

int behavior_asset_delete_graph(behavior_asset *asset, uint64_t graph_id)
{
    if (!asset || !behavior_asset_mutable(asset) || graph_id == 0) {
        return -1;
    }
    for (int i = 1; i < acount(asset->graphs); i++) {
        if (asset->graphs[i]->graph_id == graph_id) {
            graph *g = asset->graphs[i];
            adel(asset->graphs, i);
            graph_free(g);
            free(g);
            return 0;
        }
    }
    return -1;
}

int behavior_asset_mutable(const behavior_asset *asset)
{
    return asset && asset->graph && asset->graph->flow_pause_count == 0;
}

int behavior_asset_set_name(behavior_asset *asset, const char *name)
{
    if (!asset || !behavior_asset_mutable(asset) || !name) {
        return -1;
    }
    const char *old_name = asset->name;
    char *copy = graph_own_str(asset->graph, name, strlen(name));
    if (!copy) {
        return -1;
    }
    asset->name = copy;
    for (int g = 0; g < acount(asset->graphs); g++) {
        graph *target = asset->graphs[g];
        for (int i = 0; i < acount(target->nodes); i++) {
            node *n = target->nodes[i];
            for (int p = 0; p < n->input_count; p++) {
                const pin_binding *binding = n->input_pins[p].binding;
                if (!binding || binding->kind != PIN_BINDING_VARIABLE ||
                    (binding->source_asset && old_name &&
                     strcmp(binding->source_asset, old_name) != 0)) {
                    continue;
                }
                pin_binding updated = *binding;
                updated.source_asset = copy;
                graph_set_pin_binding(target, n, 0, p, &updated);
            }
        }
    }
    return 0;
}

void behavior_asset_set_description(behavior_asset *asset, const char *description)
{
    if (!asset) {
        return;
    }
    char *copy = graph_own_str(
        asset->graph,
        description ? description : "",
        description ? strlen(description) : 0
    );
    if (copy) {
        asset->description = copy;
    }
}

bool behavior_asset_set_model_reference(behavior_asset *asset, const model_reference *reference)
{
    if (asset->domain != BEHAVIOR_DOMAIN_MODEL || !behavior_asset_mutable(asset)) {
        return false;
    }
    if (!reference->image || !reference->image[0]) {
        asset->reference = (model_reference){ 0 };
        return true;
    }
    const char *segment = reference->image;
    bool relative = segment[0] != '/' && !strchr(segment, '\\') &&
                    !strchr(segment, ':');
    while (*segment && relative) {
        const char *end = strchr(segment, '/');
        size_t size = end ? (size_t)(end - segment) : strlen(segment);
        relative = size && !(size == 1 && segment[0] == '.') &&
                   !(size == 2 && segment[0] == '.' && segment[1] == '.');
        segment = end ? end + 1 : segment + size;
    }
    if (!relative || !isfinite(reference->center[0]) ||
        !isfinite(reference->center[1]) || !isfinite(reference->angle) ||
        !isfinite(reference->span) || reference->span <= 0) {
        return false;
    }
    model_reference next = *reference;
    next.image = graph_own_str(asset->graph, reference->image, strlen(reference->image));
    asset->reference = next;
    return true;
}

int behavior_asset_set_owner_type(behavior_asset *asset, const char *type_name)
{
    if (!asset || !behavior_asset_mutable(asset) || !type_name || !type_name[0]) {
        return -1;
    }
    char *copy = graph_own_str(asset->graph, type_name, strlen(type_name));
    if (!copy) {
        return -1;
    }
    asset->owner_type_name = copy;
    return 0;
}

behavior_variable *behavior_asset_add_variable(
    behavior_asset *asset,
    const char *name,
    pin_type type,
    const char *type_name,
    pin_container container,
    const pin_value *default_value,
    const char *category
)
{
    if (!asset || !behavior_asset_mutable(asset) || !name || !name[0]) {
        return NULL;
    }
    if (!behavior_asset_variable_name_unique(asset, name, 0)) {
        return NULL;
    }
    behavior_variable v = { 0 };
    v.id = graph_alloc_id(asset->graph);
    v.name = graph_own_str(asset->graph, name, strlen(name));
    v.type = type;
    v.type_name = type_name
                    ? graph_own_str(asset->graph, type_name, strlen(type_name))
                    : NULL;
    v.container = container;
    v.category = category && category[0]
                   ? graph_own_str(asset->graph, category, strlen(category))
                   : NULL;
    if (default_value) {
        v.default_value = *default_value;
        v.default_value.type = type;
        v.default_value.container = container;
        v.default_value.type_name = v.type_name;
        pin_value array = v.default_value;
        if (container >= PIN_CONTAINER_ARRAY && default_value->ptr &&
            pin_value_copy_to_graph(asset->graph, &v.default_value, &array)) {
            return NULL;
        }
        if (container == PIN_CONTAINER_SINGLE && (type == PIN_STR || type == PIN_NAME) &&
            pin_value_copy_to_graph(asset->graph, &v.default_value, default_value)) {
            return NULL;
        }
        v.default_value.type_name = v.type_name;
    } else {
        v.default_value = (pin_value){ .type = type,
                                       .container = container,
                                       .type_name = v.type_name };
        if (container == PIN_CONTAINER_SINGLE && type == PIN_NAME) {
            v.default_value.str = "";
        }
    }
    apush(asset->variables, v);
    return &asset->variables[acount(asset->variables) - 1];
}

behavior_variable *behavior_asset_find_variable_by_name(const behavior_asset *asset, const char *name)
{
    if (!asset || !name) {
        return NULL;
    }
    for (int i = 0; i < acount(asset->variables); i++) {
        if (strcmp(asset->variables[i].name, name) == 0) {
            return &asset->variables[i];
        }
    }
    return NULL;
}

behavior_variable *behavior_asset_find_variable_by_id(const behavior_asset *asset, uint64_t id)
{
    if (!asset) {
        return NULL;
    }
    for (int i = 0; i < acount(asset->variables); i++) {
        if (asset->variables[i].id == id) {
            return &asset->variables[i];
        }
    }
    return NULL;
}

int behavior_asset_set_variable_category(behavior_asset *asset, uint64_t id, const char *category)
{
    if (!asset || !behavior_asset_mutable(asset) || !category) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(asset, id);
    if (!v) {
        return -1;
    }
    v->category = graph_own_str(asset->graph, category, strlen(category));
    return v->category ? 0 : -1;
}

int behavior_asset_delete_variable(behavior_asset *asset, uint64_t id)
{
    if (!asset || !behavior_asset_mutable(asset)) {
        return -1;
    }
    for (int i = 0; i < acount(asset->variables); i++) {
        if (asset->variables[i].id == id) {
            /* VAR-7: referencing nodes keep their reference ID and become
             * orphaned; evaluation reports the lost variable. */
            adel(asset->variables, i);
            return 0;
        }
    }
    return -1;
}

int behavior_asset_set_variable_default(behavior_asset *asset, uint64_t id, const pin_value *value)
{
    if (!asset || !behavior_asset_mutable(asset) || !value) {
        return -1;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(asset, id);
    if (!v || value->type != v->type) {
        return -1;
    }
    pin_value source = *value;
    source.container = v->container;
    source.type_name = v->type_name;
    pin_value owned = source;
    /* A vector or other struct is copied, as pin defaults are. */
    if (source.container == PIN_CONTAINER_SINGLE && source.type == PIN_STRUCT &&
        source.ptr && pin_value_copy_to_graph(asset->graph, &owned, &source)) {
        return -1;
    }
    v->default_value = owned;
    v->default_value.type_name = v->type_name;
    pin_value array = v->default_value;
    if (v->container >= PIN_CONTAINER_ARRAY && value->ptr &&
        pin_value_copy_to_graph(asset->graph, &v->default_value, &array)) {
        return -1;
    }
    if (v->container == PIN_CONTAINER_SINGLE &&
        (value->type == PIN_STR || value->type == PIN_NAME) &&
        pin_value_copy_to_graph(asset->graph, &v->default_value, value)) {
        return -1;
    }
    v->default_value.type_name = v->type_name;
    return 0;
}

behavior_variable *behavior_asset_duplicate_variable(behavior_asset *asset, uint64_t id, const char *name)
{
    behavior_variable *v = behavior_asset_find_variable_by_id(asset, id);
    if (!v) {
        return NULL;
    }
    /* Adding may move the variables, and with them v. */
    behavior_variable source = *v;
    behavior_variable *copy = behavior_asset_add_variable(
        asset,
        name,
        source.type,
        source.type_name,
        source.container,
        &source.default_value,
        source.category
    );
    if (copy) {
        copy->shared = source.shared;
    }
    return copy;
}
