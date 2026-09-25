#ifndef CHIBI_EDITOR_ASSET_INDEX_H
#define CHIBI_EDITOR_ASSET_INDEX_H

#include "graph/model/pin.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct asset_index asset_index;

typedef enum
{
    ASSET_INDEX_SCENE,
    ASSET_INDEX_PROTOTYPE,
    ASSET_INDEX_BEHAVIOR,
    ASSET_INDEX_EVENT,
    ASSET_INDEX_MATERIAL,
    ASSET_INDEX_SHAPE,
    ASSET_INDEX_FIELD,
    ASSET_INDEX_LIBRARY,
    ASSET_INDEX_SYSTEM,
    ASSET_INDEX_UI,
    ASSET_INDEX_KIND_COUNT,
} asset_index_kind;

typedef enum
{
    ASSET_INDEX_REFERENCE = 0,
    ASSET_INDEX_EVENT_CONSUMER,
    ASSET_INDEX_EVENT_EMITTER,
    ASSET_INDEX_BEHAVIOR_ATTACHMENT,
    ASSET_INDEX_COMPONENT_BINDING,
    ASSET_INDEX_GRAPH_CALL,
    ASSET_INDEX_PROTOTYPE_SPAWN,
} asset_index_reference_kind;

typedef struct
{
    asset_index_kind kind;
    const char *name;
    const char *path;
    const char *identity;
    bool unresolved;
} asset_index_asset;

typedef struct
{
    asset_index_reference_kind kind;
    int source;
    int target;
    const char *detail;
    const char *component;
    const char *field;
    const char *actor;
    uint64_t graph_id;
    uint64_t node_id;
    uint64_t pin_id;
} asset_index_reference;

asset_index *asset_index_new();
void asset_index_free(asset_index *index);
void asset_index_clear(asset_index *index);

/* Session IDs keep unsaved assets addressable while their display names change. */
uint64_t asset_index_session_id(asset_index *index, asset_index_kind kind, const char *owner);
int asset_index_add_asset(
    asset_index *index,
    asset_index_kind kind,
    const char *name,
    const char *path,
    uint64_t session_id
);
int asset_index_find(const asset_index *index, asset_index_kind kind, const char *path, uint64_t session_id);
int asset_index_find_name(const asset_index *index, asset_index_kind kind, const char *name);
int asset_index_ensure_name(asset_index *index, asset_index_kind kind, const char *name);
int asset_index_add_reference(asset_index *index, asset_index_reference reference);

int asset_index_asset_count(const asset_index *index);
const asset_index_asset *asset_index_asset_at(const asset_index *index, int position);
bool asset_index_component_kind(const char *component, const char *field, asset_index_kind *kind);
int asset_index_reference_count(const asset_index *index);
const asset_index_reference *asset_index_reference_at(const asset_index *index, int position);

/* Rebuild the index from authored scene, prototype, graph and tab state. */
struct app_scene_authoring;
int app_scene_authoring_refresh_asset_index(struct app_scene_authoring *scene);
const asset_index *app_scene_authoring_asset_index(const struct app_scene_authoring *scene);
int app_scene_authoring_open_asset(struct app_scene_authoring *scene, int asset);

#endif
