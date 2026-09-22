#include "editor/workspace/asset_index.h"

#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "editor/ui/search.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/events.h"
#include "graph/document/serialize.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/graph.h"
#include "graph/model/node.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

typedef struct
{
    asset_index_kind kind;
    char *owner;
    uint64_t id;
} asset_session;

struct asset_index
{
    asset_index_asset *assets;
    asset_index_reference *references;
    asset_index_binding_usage *binding_usages;
    asset_session *sessions;
    uint64_t next_session;
};

static char *session_identity(asset_index_kind kind, uint64_t id)
{
    char value[64];
    snprintf(value, sizeof value, "session:%d:%" PRIu64, kind, id);
    return xstrdup(value);
}

asset_index *asset_index_new()
{
    asset_index *index = xcalloc(1, sizeof *index);
    index->next_session = 1;
    return index;
}

static void free_asset(asset_index_asset *asset)
{
    free((char *)asset->name);
    free((char *)asset->path);
    free((char *)asset->identity);
}

static void free_reference(asset_index_reference *reference)
{
    free((char *)reference->detail);
    free((char *)reference->component);
    free((char *)reference->field);
    free((char *)reference->actor);
}

static void free_binding_usage(asset_index_binding_usage *usage)
{
    free((char *)usage->variable_name);
    free((char *)usage->source_asset);
    free((char *)usage->component);
    free((char *)usage->field);
}

void asset_index_clear(asset_index *index)
{
    if (!index) {
        return;
    }
    for (int i = 0; i < acount(index->assets); i++) {
        free_asset(&index->assets[i]);
    }
    for (int i = 0; i < acount(index->references); i++) {
        free_reference(&index->references[i]);
    }
    for (int i = 0; i < acount(index->binding_usages); i++) {
        free_binding_usage(&index->binding_usages[i]);
    }
    asetlen(index->assets, 0);
    asetlen(index->references, 0);
    asetlen(index->binding_usages, 0);
}

void asset_index_free(asset_index *index)
{
    if (!index) {
        return;
    }
    asset_index_clear(index);
    afree(index->assets);
    afree(index->references);
    afree(index->binding_usages);
    for (int i = 0; i < acount(index->sessions); i++) {
        free(index->sessions[i].owner);
    }
    afree(index->sessions);
    free(index);
}

uint64_t asset_index_session_id(asset_index *index, asset_index_kind kind, const char *owner)
{
    if (!index || !owner) {
        return 0;
    }
    for (int i = 0; i < acount(index->sessions); i++) {
        if (index->sessions[i].kind == kind &&
            !strcmp(index->sessions[i].owner, owner)) {
            return index->sessions[i].id;
        }
    }
    uint64_t id = index->next_session++;
    apush(index->sessions, ((asset_session){ kind, xstrdup(owner), id }));
    return id;
}

static bool same_path(const asset_index_asset *asset, const char *path)
{
    if (!path || !path[0] || !asset->path) {
        return false;
    }
    char *left = realpath(asset->path, NULL);
    char *right = realpath(path, NULL);
    bool same = left && right ? strcmp(left, right) == 0
                              : strcmp(asset->path, path) == 0;
    free(left);
    free(right);
    return same;
}

static char *path_identity(const char *path)
{
    if (!path || !path[0]) {
        return NULL;
    }
    char *canonical = realpath(path, NULL);
    if (canonical) {
        return canonical;
    }
    const char *relative = path;
    while (relative[0] == '.' && relative[1] == '/') {
        relative += 2;
    }
    return xstrdup(relative);
}

int asset_index_find(const asset_index *index, asset_index_kind kind, const char *path, uint64_t session_id)
{
    if (!index) {
        return -1;
    }
    for (int i = 0; i < acount(index->assets); i++) {
        const asset_index_asset *asset = &index->assets[i];
        if (asset->kind != kind) {
            continue;
        }
        if (same_path(asset, path)) {
            return i;
        }
        if (!path && session_id) {
            char *identity = session_identity(kind, session_id);
            bool same = asset->identity && strcmp(asset->identity, identity) == 0;
            free(identity);
            if (same) {
                return i;
            }
        }
    }
    return -1;
}

int asset_index_find_name(const asset_index *index, asset_index_kind kind, const char *name)
{
    if (!index || !name || !name[0]) {
        return -1;
    }
    for (int i = 0; i < acount(index->assets); i++) {
        const asset_index_asset *asset = &index->assets[i];
        if (asset->kind == kind && asset->name && strcmp(asset->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int unresolved_name(const asset_index *index, asset_index_kind kind, const char *name)
{
    if (!index || !name || !name[0]) {
        return -1;
    }
    for (int i = 0; i < acount(index->assets); i++) {
        const asset_index_asset *asset = &index->assets[i];
        if (asset->kind == kind && asset->unresolved && asset->name &&
            strcmp(asset->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int add_asset_record(
    asset_index *index,
    asset_index_kind kind,
    const char *name,
    const char *path,
    uint64_t session_id,
    bool unresolved
)
{
    int existing = path ? asset_index_find(index, kind, path, 0)
                        : asset_index_find(index, kind, NULL, session_id);
    if (existing < 0 && !path) {
        existing = unresolved_name(index, kind, name);
    }
    if (existing < 0 && path && !session_id) {
        existing = asset_index_find_name(index, kind, name);
        if (existing >= 0 && index->assets[existing].path) {
            existing = -1;
        }
    }
    if (existing >= 0) {
        asset_index_asset *asset = &index->assets[existing];
        if (path && (!asset->path || asset->unresolved)) {
            free((char *)asset->path);
            free((char *)asset->identity);
            asset->path = xstrdup(path);
            asset->identity = path_identity(path);
            asset->unresolved = false;
        }
        if (!path && session_id && asset->unresolved) {
            free((char *)asset->identity);
            asset->identity = session_identity(kind, session_id);
            asset->unresolved = false;
        }
        if (name && name[0] &&
            (!asset->name || asset->unresolved || strcmp(asset->name, name) != 0)) {
            free((char *)asset->name);
            asset->name = xstrdup(name);
        }
        return existing;
    }
    asset_index_asset asset = {
        .kind = kind,
        .name = xstrdup(name ? name : ""),
        .path = path ? xstrdup(path) : NULL,
        .identity = path ? path_identity(path)
                  : session_id
                      ? session_identity(kind, session_id)
                      : session_identity(kind, (uint64_t)acount(index->assets) + 1),
        .unresolved = unresolved,
    };
    apush(index->assets, asset);
    return acount(index->assets) - 1;
}

int asset_index_add_asset(
    asset_index *index,
    asset_index_kind kind,
    const char *name,
    const char *path,
    uint64_t session_id
)
{
    if (!index || !name || !name[0]) {
        return -1;
    }
    return add_asset_record(index, kind, name, path, session_id, false);
}

int asset_index_ensure_name(asset_index *index, asset_index_kind kind, const char *name)
{
    if (!index || !name || !name[0]) {
        return -1;
    }
    int found = asset_index_find_name(index, kind, name);
    return found >= 0 ? found : add_asset_record(index, kind, name, NULL, 0, true);
}

int asset_index_add_reference(asset_index *index, asset_index_reference reference)
{
    if (!index || reference.source < 0 || reference.source >= acount(index->assets) ||
        reference.target < 0 || reference.target >= acount(index->assets)) {
        return -1;
    }
    apush(
        index->references,
        ((asset_index_reference){
            .kind = reference.kind,
            .source = reference.source,
            .target = reference.target,
            .detail = reference.detail ? xstrdup(reference.detail) : NULL,
            .component = reference.component ? xstrdup(reference.component) : NULL,
            .field = reference.field ? xstrdup(reference.field) : NULL,
            .actor = reference.actor ? xstrdup(reference.actor) : NULL,
            .graph_id = reference.graph_id,
            .node_id = reference.node_id,
            .pin_id = reference.pin_id,
        })
    );
    return 0;
}

static int asset_index_add_binding_usage(asset_index *index, asset_index_binding_usage usage)
{
    if (!index || usage.source < 0 || usage.source >= acount(index->assets) ||
        !usage.node_id || !usage.pin_id) {
        return -1;
    }
    apush(
        index->binding_usages,
        ((asset_index_binding_usage){
            .source = usage.source,
            .kind = usage.kind,
            .variable_id = usage.variable_id,
            .variable_name = usage.variable_name ? xstrdup(usage.variable_name) : NULL,
            .source_asset = usage.source_asset ? xstrdup(usage.source_asset) : NULL,
            .component = usage.component ? xstrdup(usage.component) : NULL,
            .field = usage.field ? xstrdup(usage.field) : NULL,
            .graph_id = usage.graph_id,
            .node_id = usage.node_id,
            .pin_id = usage.pin_id,
            .dormant = usage.dormant,
        })
    );
    return 0;
}

int asset_index_binding_usage_count(const asset_index *index)
{
    return index ? acount(index->binding_usages) : 0;
}

const asset_index_binding_usage *asset_index_binding_usage_at(const asset_index *index, int position)
{
    return index && position >= 0 && position < acount(index->binding_usages)
             ? &index->binding_usages[position]
             : NULL;
}

int asset_index_asset_count(const asset_index *index)
{
    return index ? acount(index->assets) : 0;
}

const asset_index_asset *asset_index_asset_at(const asset_index *index, int position)
{
    return index && position >= 0 && position < acount(index->assets)
             ? &index->assets[position]
             : NULL;
}

int asset_index_reference_count(const asset_index *index)
{
    return index ? acount(index->references) : 0;
}

const asset_index_reference *asset_index_reference_at(const asset_index *index, int position)
{
    return index && position >= 0 && position < acount(index->references)
             ? &index->references[position]
             : NULL;
}

int asset_index_score(const asset_index_asset *asset, const char *query)
{
    return editor_search_score(query, asset->name, asset->path);
}

static asset_index_kind kind_for_domain(behavior_domain domain)
{
    switch (domain) {
        case BEHAVIOR_DOMAIN_EVENT: return ASSET_INDEX_BEHAVIOR;
        case BEHAVIOR_DOMAIN_LIBRARY:
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY: return ASSET_INDEX_LIBRARY;
        case BEHAVIOR_DOMAIN_MATERIAL: return ASSET_INDEX_MATERIAL;
        case BEHAVIOR_DOMAIN_FIELD: return ASSET_INDEX_FIELD;
        case BEHAVIOR_DOMAIN_SHAPE: return ASSET_INDEX_SHAPE;
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION: return ASSET_INDEX_EVENT;
    }
    return ASSET_INDEX_BEHAVIOR;
}

static const char *pin_string(const pin_spec *pin)
{
    if (!pin || pin->container != PIN_CONTAINER_SINGLE) {
        return NULL;
    }
    if (pin->default_value.type != PIN_NAME && pin->default_value.type != PIN_STR) {
        return NULL;
    }
    return pin->default_value.str;
}

static const char *graph_node_pin_string(const node *node, const char *name, uint64_t *pin_id)
{
    for (int i = 0; i < node->input_count; i++) {
        const pin_spec *pin = &node->input_pins[i];
        if (!strcmp(pin->name, name)) {
            if (pin_id) {
                *pin_id = pin->id;
            }
            return pin_string(pin);
        }
    }
    return NULL;
}

static void add_graph_references(asset_index *index, int source_id, const behavior_asset *asset)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *graph = asset->graphs[i];
        if (!graph || graph->math_expression) {
            continue;
        }
        for (int j = 0; j < acount(graph->nodes); j++) {
            const node *node = graph->nodes[j];
            if (!node || node->disabled || !node->desc) {
                continue;
            }
            const char *node_name = node->desc->name;
            if (node->reference_asset && node->reference_asset[0]) {
                int target = asset_index_ensure_name(index, ASSET_INDEX_LIBRARY, node->reference_asset);
                asset_index_add_reference(
                    index,
                    (asset_index_reference){
                        .kind = ASSET_INDEX_GRAPH_CALL,
                        .source = source_id,
                        .target = target,
                        .detail = graph->name,
                        .graph_id = graph->graph_id,
                        .node_id = node->id,
                    }
                );
            }
            if (!strcmp(node_name, "Spawn Prototype")) {
                uint64_t pin_id = 0;
                const char *prototype = graph_node_pin_string(node, "Prototype", &pin_id);
                if (prototype && prototype[0]) {
                    int target = asset_index_ensure_name(index, ASSET_INDEX_PROTOTYPE, prototype);
                    asset_index_add_reference(
                        index,
                        (asset_index_reference){
                            .kind = ASSET_INDEX_PROTOTYPE_SPAWN,
                            .source = source_id,
                            .target = target,
                            .detail = graph->name,
                            .field = "Prototype",
                            .graph_id = graph->graph_id,
                            .node_id = node->id,
                            .pin_id = pin_id,
                        }
                    );
                }
            }
            for (int pin = 0; pin < node->input_count; pin++) {
                const pin_spec *input = &node->input_pins[pin];
                const pin_binding *binding = input->binding;
                if (!binding || binding->kind == PIN_BINDING_LOCAL ||
                    input->kind != PIN_DATA) {
                    continue;
                }
                behavior_variable *variable = binding->kind == PIN_BINDING_VARIABLE
                                                ? behavior_asset_find_variable_by_id(
                                                      asset,
                                                      binding->variable_id
                                                  )
                                                : NULL;
                struct node *source = NULL;
                int source_pin = -1;
                asset_index_add_binding_usage(
                    index,
                    (asset_index_binding_usage){
                        .source = source_id,
                        .kind = binding->kind,
                        .variable_id = binding->variable_id,
                        .variable_name = variable ? variable->name : NULL,
                        .source_asset = binding->source_asset,
                        .component = binding->component_key,
                        .field = binding->field_name,
                        .graph_id = graph->graph_id,
                        .node_id = node->id,
                        .pin_id = input->id,
                        .dormant = graph_input_source(graph, node, pin, &source, &source_pin),
                    }
                );
            }
            if (node->reference_name && node->reference_name[0] &&
                !graph_event_host_name(node->reference_name)) {
                bool event_node = !strcmp(node_name, "Event");
                bool emitter = !event_node && (!strcmp(node_name, "Call Event") ||
                                               !strcmp(node_name, "Send Event"));
                if (event_node || emitter) {
                    int target = asset_index_ensure_name(index, ASSET_INDEX_EVENT, node->reference_name);
                    asset_index_add_reference(
                        index,
                        (asset_index_reference){
                            .kind = emitter ? ASSET_INDEX_EVENT_EMITTER
                                            : ASSET_INDEX_EVENT_CONSUMER,
                            .source = source_id,
                            .target = target,
                            .detail = graph->name,
                            .graph_id = graph->graph_id,
                            .node_id = node->id,
                        }
                    );
                }
            }
        }
    }
}

static bool override_has_field(const char *json, const char *component, const char *field)
{
    yyjson_doc *document = json ? yyjson_read(json, strlen(json), 0) : NULL;
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *components = yyjson_is_obj(root) ? yyjson_obj_get(root, "components")
                                                 : NULL;
    if (!components) {
        components = root;
    }
    yyjson_val *value = yyjson_is_obj(components)
                          ? yyjson_obj_get(components, component)
                          : NULL;
    if (!value && yyjson_is_obj(components)) {
        char generated_name[128];
        snprintf(generated_name, sizeof generated_name, "GAME.%s", component);
        value = yyjson_obj_get(components, generated_name);
    }
    bool present = yyjson_is_obj(value) && yyjson_obj_get(value, field) != NULL;
    yyjson_doc_free(document);
    return present;
}

static const struct {const char *component, *field; asset_index_kind kind;} component_bindings[] = {
    {"MaterialTerrain", "field", ASSET_INDEX_FIELD},
    {"EngineSystem", "asset", ASSET_INDEX_SYSTEM},
    {"GameShader", "material", ASSET_INDEX_MATERIAL},
    {"GameShader", "shape", ASSET_INDEX_SHAPE},
};

bool asset_index_component_kind(const char *component, const char *field, asset_index_kind *kind)
{
    for (int i = 0; i < (int)(sizeof component_bindings / sizeof component_bindings[0]); i++) {
        if (!strcmp(component, component_bindings[i].component) && !strcmp(field, component_bindings[i].field)) {
            *kind = component_bindings[i].kind;
            return true;
        }
    }
    return false;
}

static void add_component_references(
    asset_index *index,
    int source_id,
    const game_prototype *prototype,
    const char *actor,
    const char *overrides_json
)
{
    pin_value value;
    for (int i = 0; i < (int)(sizeof component_bindings / sizeof component_bindings[0]); i++) {
        const char *component = component_bindings[i].component, *field = component_bindings[i].field;
        if (game_prototype_read_field(prototype, component, field, &value) || !value.str || !value.str[0] || override_has_field(overrides_json, component, field)) continue;
        int target = asset_index_ensure_name(index, component_bindings[i].kind, value.str);
        asset_index_add_reference(index, (asset_index_reference){.kind = ASSET_INDEX_COMPONENT_BINDING,
            .source = source_id, .target = target, .detail = component, .component = component, .field = field, .actor = actor});
    }
    if (override_has_field(overrides_json, "GameMeshParts", "parts") ||
        game_prototype_read_field(prototype, "GameMeshParts", "parts", &value) != 0 ||
        !value.str || !value.str[0]) {
        return;
    }
    yyjson_doc *parsed = yyjson_read(value.str, strlen(value.str), 0);
    yyjson_val *root = parsed ? yyjson_doc_get_root(parsed) : NULL;
    if (!yyjson_is_arr(root)) {
        yyjson_doc_free(parsed);
        return;
    }
    yyjson_arr_iter iterator = yyjson_arr_iter_with(root);
    yyjson_val *part;
    int number = 0;
    while ((part = yyjson_arr_iter_next(&iterator))) {
        yyjson_val *material = yyjson_is_obj(part) ? yyjson_obj_get(part, "material")
                                                   : NULL;
        if (yyjson_is_str(material) && yyjson_get_len(material)) {
            char detail[64];
            snprintf(detail, sizeof detail, "Mesh part %d", number + 1);
            int target = asset_index_ensure_name(index, ASSET_INDEX_MATERIAL, yyjson_get_str(material));
            asset_index_add_reference(
                index,
                (asset_index_reference){
                    .kind = ASSET_INDEX_COMPONENT_BINDING,
                    .source = source_id,
                    .target = target,
                    .detail = detail,
                    .component = "GameMeshParts",
                    .field = "parts.material",
                    .actor = actor,
                }
            );
        }
        number++;
    }
    yyjson_doc_free(parsed);
}

static int scene_asset_for_tab(const asset_index *index, const scene_document_tab *tab)
{
    uint64_t session = tab->path
                         ? 0
                         : asset_index_session_id((asset_index *)index, ASSET_INDEX_SCENE, tab->key);
    return asset_index_find(index, ASSET_INDEX_SCENE, tab->path, session);
}

static void add_override_component_references(
    app_scene_authoring *scene,
    asset_index *index,
    int source_id,
    const char *components_json,
    const char *actor
)
{
    if (!components_json || !components_json[0]) {
        return;
    }
    char error[256];
    size_t size = strlen(components_json) + 64;
    char *wrapper = xmalloc(size);
    snprintf(wrapper, size, "{\"name\":\"SceneInstance\",\"components\":%s}", components_json);
    game_prototype *overrides = game_prototype_load(wrapper, &scene->registry, error, sizeof error);
    free(wrapper);
    if (!overrides) {
        return;
    }
    add_component_references(index, source_id, overrides, actor, NULL);
    game_prototype_free(overrides);
}

static void add_scene_document_references(
    app_scene_authoring *scene,
    asset_index *index,
    int source_id,
    const char *json
)
{
    if (!json) {
        return;
    }
    char error[256];
    game_scene_document *document = game_scene_document_load(json, error, sizeof error);
    if (!document) {
        return;
    }
    for (int i = 0; i < acount(document->entities); i++) {
        game_scene_entity *entity = &document->entities[i];
        int prototype = asset_index_ensure_name(
            index,
            ASSET_INDEX_PROTOTYPE,
            entity->prototype
        );
        asset_index_add_reference(
            index,
            (asset_index_reference){
                .kind = ASSET_INDEX_REFERENCE,
                .source = source_id,
                .target = prototype,
                .detail = "Scene instance",
                .actor = entity->name,
            }
        );
        scene_prototype_asset *base = find_prototype(scene, entity->prototype);
        if (base && base->prototype) {
            add_component_references(
                index,
                source_id,
                base->prototype,
                entity->name,
                entity->components_json
            );
        }
        for (int j = 0; j < acount(entity->behaviors); j++) {
            int behavior = asset_index_ensure_name(
                index,
                ASSET_INDEX_BEHAVIOR,
                entity->behaviors[j]
            );
            asset_index_add_reference(
                index,
                (asset_index_reference){
                    .kind = ASSET_INDEX_BEHAVIOR_ATTACHMENT,
                    .source = source_id,
                    .target = behavior,
                    .detail = "Scene behavior",
                    .actor = entity->name,
                }
            );
        }
        add_override_component_references(
            scene,
            index,
            source_id,
            entity->components_json,
            entity->name
        );
    }
    game_scene_document_free(document);
}

static int add_scene_tab_asset(app_scene_authoring *scene, asset_index *index, scene_document_tab *tab)
{
    const char *name = tab->label && tab->label[0] ? tab->label : "Scene";
    uint64_t session = tab->path
                         ? 0
                         : asset_index_session_id(index, ASSET_INDEX_SCENE, tab->key);
    int source = asset_index_add_asset(index, ASSET_INDEX_SCENE, name, tab->path, session);
    add_scene_document_references(scene, index, source, tab->scene_json);
    return source;
}

static int add_system_tab_asset(asset_index *index, scene_document_tab *tab)
{
    const char *name = tab->label && tab->label[0] ? tab->label : "System";
    uint64_t session = tab->path
                         ? 0
                         : asset_index_session_id(index, ASSET_INDEX_SYSTEM, tab->key);
    return asset_index_add_asset(index, ASSET_INDEX_SYSTEM, name, tab->path, session);
}

static void add_live_definitions(asset_index *index, app_scene_authoring *scene)
{
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        game_live_definition *definition = game_live_definition_at(scene->live, i);
        asset_index_kind kind = kind_for_domain(definition->working->domain);
        uint64_t session = definition->path
                             ? 0
                             : asset_index_session_id(index, kind, definition->name);
        int source = asset_index_add_asset(
            index,
            kind,
            definition->name,
            definition->path,
            session
        );
        const behavior_asset *working = definition->working;
        if (definition == scene->definition && scene->graph_editor &&
            definition->working) {
            const behavior_asset *current = editor_behavior_asset(scene->graph_editor);
            if (current && current->name && definition->working->name &&
                current->domain == definition->working->domain &&
                !strcmp(current->name, definition->working->name)) {
                working = current;
            }
        }
        add_graph_references(index, source, working);
    }
}

static asset_index_kind content_asset_kind(const app_scene_authoring *scene, const char *path)
{
    static const struct
    {
        const char *folder;
        asset_index_kind kind;
    } folders[] = {
        { "scenes", ASSET_INDEX_SCENE },
        { "prototypes", ASSET_INDEX_PROTOTYPE },
        { "behaviors", ASSET_INDEX_BEHAVIOR },
        { "events", ASSET_INDEX_EVENT },
        { "materials", ASSET_INDEX_MATERIAL },
        { "shapes", ASSET_INDEX_SHAPE },
        { "fields", ASSET_INDEX_FIELD },
        { "libraries", ASSET_INDEX_LIBRARY },
        { "shader-libraries", ASSET_INDEX_LIBRARY },
        { "systems", ASSET_INDEX_SYSTEM },
    };
    for (int i = 0; i < (int)(sizeof folders / sizeof *folders); i++) {
        if (content_path_in_folder(scene, path, folders[i].folder)) {
            return folders[i].kind;
        }
    }
    return ASSET_INDEX_KIND_COUNT;
}

static const char *content_asset_name(const char *path)
{
    const char *base = content_basename(path);
    static char name[256];
    snprintf(name, sizeof name, "%s", base);
    char *extension = strrchr(name, '.');
    if (extension && !strcmp(extension, ".json")) {
        *extension = '\0';
    }
    return name;
}

static void add_content_catalog_assets(asset_index *index, app_scene_authoring *scene)
{
    for (int i = 0; i < acount(scene->content); i++) {
        scene_content_entry *entry = &scene->content[i];
        if (entry->directory || entry->image) {
            continue;
        }
        asset_index_kind kind = content_asset_kind(scene, entry->path);
        if (kind != ASSET_INDEX_KIND_COUNT) {
            asset_index_add_asset(
                index,
                kind,
                content_asset_name(entry->path),
                entry->path,
                0
            );
        }
    }
}

static void add_prototypes(asset_index *index, app_scene_authoring *scene)
{
    for (int i = 0; i < acount(scene->prototypes); i++) {
        scene_prototype_asset *asset = &scene->prototypes[i];
        uint64_t session = asset->path ? 0
                                       : asset_index_session_id(
                                             index,
                                             ASSET_INDEX_PROTOTYPE,
                                             game_prototype_identity(asset->prototype)
                                         );
        int source = asset_index_add_asset(
            index,
            ASSET_INDEX_PROTOTYPE,
            asset->prototype->name,
            asset->path,
            session
        );
        for (int j = 0; j < acount(asset->prototype->graphs); j++) {
            int target = asset_index_ensure_name(
                index,
                ASSET_INDEX_BEHAVIOR,
                asset->prototype->graphs[j]
            );
            asset_index_add_reference(
                index,
                (asset_index_reference){
                    .kind = ASSET_INDEX_BEHAVIOR_ATTACHMENT,
                    .source = source,
                    .target = target,
                    .detail = "Prototype behavior",
                }
            );
        }
        add_component_references(index, source, asset->prototype, NULL, NULL);
    }
}

static void add_active_scene_references(app_scene_authoring *scene, asset_index *index)
{
    int source = -1;
    for (int i = 0; i < acount(scene->tabs); i++) {
        scene_document_tab *tab = &scene->tabs[i];
        if (tab->kind != APP_EDITOR_DOCUMENT_SCENE) {
            continue;
        }
        if (scene->current_scene_tab_key && tab->key &&
            !strcmp(scene->current_scene_tab_key, tab->key)) {
            source = scene_asset_for_tab(index, tab);
            break;
        }
    }
    if (source < 0) {
        return;
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        scene_actor *actor = &scene->actors[i];
        const game_prototype *snapshot = scene->runtime_snapshot_valid
                                           ? actor->runtime_snapshot
                                           : NULL;
        assert(!scene->runtime_snapshot_valid || snapshot);
        int prototype = asset_index_ensure_name(
            index,
            ASSET_INDEX_PROTOTYPE,
            actor->prototype_name
        );
        asset_index_add_reference(
            index,
            (asset_index_reference){
                .kind = ASSET_INDEX_REFERENCE,
                .source = source,
                .target = prototype,
                .detail = "Scene instance",
                .actor = actor->name,
            }
        );
        if (snapshot) {
            for (int j = 0; j < acount(snapshot->graphs); j++) {
                int behavior = asset_index_ensure_name(
                    index,
                    ASSET_INDEX_BEHAVIOR,
                    snapshot->graphs[j]
                );
                asset_index_add_reference(
                    index,
                    (asset_index_reference){
                        .kind = ASSET_INDEX_BEHAVIOR_ATTACHMENT,
                        .source = source,
                        .target = behavior,
                        .detail = "Scene behavior",
                        .actor = actor->name,
                    }
                );
            }
        } else {
            int count = game_behavior_attachment_count_for_entity(
                scene->live->behaviors,
                actor->entity
            );
            for (int j = 0; j < count; j++) {
                game_behavior_attachment_info info;
                if (game_behavior_attachment_info_at(
                        scene->live->behaviors,
                        actor->entity,
                        j,
                        &info
                    ) != 0) {
                    continue;
                }
                int behavior = asset_index_ensure_name(index, ASSET_INDEX_BEHAVIOR, info.asset_name);
                asset_index_add_reference(
                    index,
                    (asset_index_reference){
                        .kind = ASSET_INDEX_BEHAVIOR_ATTACHMENT,
                        .source = source,
                        .target = behavior,
                        .detail = "Scene behavior",
                        .actor = actor->name,
                    }
                );
            }
        }
        char *overrides = snapshot
                            ? game_prototype_overrides_json(actor->base, snapshot)
                            : game_prototype_capture_overrides_json(
                                  actor->base,
                                  scene->world,
                                  actor->entity
                              );
        add_component_references(index, source, actor->base, actor->name, overrides);
        add_override_component_references(scene, index, source, overrides, actor->name);
        free(overrides);
    }
}

static void add_system_references(app_scene_authoring *scene, asset_index *index)
{
    for (int i = 0; i < acount(scene->tabs); i++) {
        scene_document_tab *tab = &scene->tabs[i];
        if (tab->kind != APP_EDITOR_DOCUMENT_SYSTEM) {
            continue;
        }
        int source = add_system_tab_asset(index, tab);
        scene_document_tab *active = scene->active_tab >= 0 &&
                                             scene->active_tab < acount(scene->tabs)
                                       ? &scene->tabs[scene->active_tab]
                                       : NULL;
        if (active == tab && scene->system_document) {
            const behavior_asset *working = scene->graph_document_kind == APP_EDITOR_DOCUMENT_SYSTEM
                                              ? editor_behavior_asset(scene->graph_editor)
                                              : game_system_document_behavior_asset(
                                                    scene->system_document
                                                );
            add_graph_references(index, source, working);
            continue;
        }
        behavior_asset *asset = tab->system_json
                                  ? behavior_asset_load_json(tab->system_json)
                                  : NULL;
        if (asset) {
            add_graph_references(index, source, asset);
            behavior_asset_free(asset);
        }
    }
}

int app_scene_authoring_refresh_asset_index(app_scene_authoring *scene)
{
    if (!scene) {
        return -1;
    }
    if (!scene->asset_index) {
        scene->asset_index = asset_index_new();
    }
    asset_index *index = scene->asset_index;
    asset_index_clear(index);
    add_content_catalog_assets(index, scene);
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE) {
            scene_document_tab *tab = &scene->tabs[i];
            bool current = scene->current_scene_tab_key && tab->key &&
                           !strcmp(scene->current_scene_tab_key, tab->key);
            if (current) {
                const char *name = tab->label && tab->label[0] ? tab->label : "Scene";
                uint64_t session = tab->path
                                     ? 0
                                     : asset_index_session_id(index, ASSET_INDEX_SCENE, tab->key);
                asset_index_add_asset(index, ASSET_INDEX_SCENE, name, tab->path, session);
            } else {
                add_scene_tab_asset(scene, index, tab);
            }
        }
    }
    add_live_definitions(index, scene);
    add_prototypes(index, scene);
    add_active_scene_references(scene, index);
    add_system_references(scene, index);
    scene->asset_index_built_generation = scene->asset_index_generation;
    return 0;
}

const asset_index *app_scene_authoring_asset_index(const app_scene_authoring *scene)
{
    return scene ? scene->asset_index : NULL;
}

int app_scene_authoring_open_asset(app_scene_authoring *scene, int asset_position)
{
    const asset_index_asset *asset = scene && scene->asset_index
                                       ? asset_index_asset_at(scene->asset_index, asset_position)
                                       : NULL;
    if (!asset || asset->unresolved) {
        return -1;
    }
    if (asset->path) {
        content_select_path(scene, asset->path);
    }
    switch (asset->kind) {
        case ASSET_INDEX_SCENE:
        case ASSET_INDEX_SYSTEM:
            for (int i = 0; i < acount(scene->tabs); i++) {
                scene_document_tab *tab = &scene->tabs[i];
                app_editor_document_kind kind = asset->kind == ASSET_INDEX_SCENE
                                                  ? APP_EDITOR_DOCUMENT_SCENE
                                                  : APP_EDITOR_DOCUMENT_SYSTEM;
                if (tab->kind != kind) {
                    continue;
                }
                uint64_t session = tab->path ? 0
                                             : asset_index_session_id(
                                                   scene->asset_index,
                                                   asset->kind,
                                                   tab->key
                                               );
                bool same = asset_index_find(scene->asset_index, asset->kind, tab->path, session) ==
                            asset_position;
                if (same) {
                    return scene_tabs_activate(scene, i);
                }
            }
            if (asset->kind == ASSET_INDEX_SCENE && asset->path) {
                return scene_tabs_open_scene(scene, asset->path);
            }
            if (asset->kind == ASSET_INDEX_SYSTEM && asset->path) {
                return open_system_asset(scene, asset->path);
            }
            return -1;
        case ASSET_INDEX_PROTOTYPE:
            return app_scene_authoring_open_prototype(scene, asset->name);
        case ASSET_INDEX_BEHAVIOR:
        case ASSET_INDEX_EVENT:
        case ASSET_INDEX_MATERIAL:
        case ASSET_INDEX_SHAPE:
        case ASSET_INDEX_FIELD:
        case ASSET_INDEX_LIBRARY:
            return app_scene_authoring_open_behavior(scene, asset->name);
        case ASSET_INDEX_KIND_COUNT: break;
    }
    return -1;
}
