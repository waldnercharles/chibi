#include "assets/new_asset.h"

#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "assets/system_document.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/shader/shader_graph.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    NAME_MAX_LENGTH = 63,
    SHAPE_HALF_EXTENT = 32,
};

static const char *const FOLDERS[NEW_ASSET_KIND_COUNT] = {
    [NEW_ASSET_SCENE] = "scenes",
    [NEW_ASSET_PROTOTYPE] = "prototypes",
    [NEW_ASSET_BEHAVIOR] = "behaviors",
    [NEW_ASSET_EVENT] = "events",
    [NEW_ASSET_SYSTEM] = "systems",
    [NEW_ASSET_LIBRARY] = "libraries",
    [NEW_ASSET_MATERIAL] = "materials",
    [NEW_ASSET_SHAPE] = "shapes",
    [NEW_ASSET_FIELD] = "fields",
    [NEW_ASSET_SHADER_LIBRARY] = "shader-libraries",
};

const char *new_asset_folder(new_asset_kind kind)
{
    return FOLDERS[kind];
}

bool new_asset_name_valid(const char *name, char *error, size_t size)
{
    bool valid = isalpha((unsigned char)name[0]) && strlen(name) <= NAME_MAX_LENGTH;
    for (const char *c = name; valid && *c; c++) {
        valid = isalnum((unsigned char)*c) || *c == '_' || *c == '-';
    }
    if (!valid) {
        snprintf(error, size, name[0] ? "Start with a letter; use letters, digits, - and _" : "Enter a name");
    }
    return valid;
}

behavior_asset *new_asset_graph(behavior_domain domain, const char *name)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = domain;
    node *output = NULL;
    switch (domain) {
        case BEHAVIOR_DOMAIN_MATERIAL:
            output = shader_graph_add_output(asset->graph, false);
            break;
        case BEHAVIOR_DOMAIN_SHAPE:
            asset->bounds[0] = asset->bounds[1] = SHAPE_HALF_EXTENT;
            output = shader_graph_add_output(asset->graph, true);
            break;
        case BEHAVIOR_DOMAIN_FIELD:
            output = graph_add_node(asset->graph, registry_node_by_name("Field Output"));
            break;
        case BEHAVIOR_DOMAIN_EVENT:
        case BEHAVIOR_DOMAIN_LIBRARY:
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION:
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY: return asset;
    }
    /* Automatic: the editor arranges it, and the file holds no position. */
    output->placed = false;
    return asset;
}

static char *graph_json(behavior_domain domain, const char *name)
{
    behavior_asset *asset = new_asset_graph(domain, name);
    char *json = behavior_asset_save_json(asset);
    behavior_asset_free(asset);
    return json;
}

char *new_asset_json(new_asset_kind kind, const char *name)
{
    switch (kind) {
        case NEW_ASSET_SCENE: {
            game_scene_document *scene = game_scene_document_new(name);
            char *json = game_scene_document_save(scene);
            game_scene_document_free(scene);
            return json;
        }
        case NEW_ASSET_PROTOTYPE: {
            game_prototype *prototype = game_prototype_new(name);
            char *json = game_prototype_save(prototype);
            game_prototype_free(prototype);
            return json;
        }
        case NEW_ASSET_SYSTEM: {
            /* No query: the system runs on what its graph reads. */
            game_system_document *system = game_system_document_new(name);
            char *json = game_system_document_save(system, NULL, NULL, NULL);
            game_system_document_free(system);
            return json;
        }
        case NEW_ASSET_BEHAVIOR: return graph_json(BEHAVIOR_DOMAIN_EVENT, name);
        case NEW_ASSET_EVENT:
            return graph_json(BEHAVIOR_DOMAIN_EVENT_DEFINITION, name);
        case NEW_ASSET_LIBRARY:
            return graph_json(BEHAVIOR_DOMAIN_LIBRARY, name);
        case NEW_ASSET_MATERIAL:
            return graph_json(BEHAVIOR_DOMAIN_MATERIAL, name);
        case NEW_ASSET_SHAPE: return graph_json(BEHAVIOR_DOMAIN_SHAPE, name);
        case NEW_ASSET_FIELD: return graph_json(BEHAVIOR_DOMAIN_FIELD, name);
        case NEW_ASSET_SHADER_LIBRARY:
            return graph_json(BEHAVIOR_DOMAIN_SHADER_LIBRARY, name);
        case NEW_ASSET_KIND_COUNT: break;
    }
    assert(!"a new asset kind");
    return NULL;
}
