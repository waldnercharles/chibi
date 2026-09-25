#ifndef CHIBI_EDITOR_ASSET_INDEX_H
#define CHIBI_EDITOR_ASSET_INDEX_H

#include "graph/model/behavior_asset.h"
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
    ASSET_INDEX_VARIABLE_READ,  /* Get Variable, or an input bound to one */
    ASSET_INDEX_VARIABLE_WRITE, /* Set Variable */
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
    uint64_t symbol_id; /* the called graph or the variable within the target */
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
/* The kind that indexes a graph asset of domain. */
asset_index_kind asset_index_domain_kind(behavior_domain domain);
/* Indexes the calls, spawns, events and variable uses in source's graphs.
 * A local call or variable targets source itself. */
void asset_index_add_graph_references(asset_index *index, int source, const behavior_asset *asset);

int asset_index_asset_count(const asset_index *index);
const asset_index_asset *asset_index_asset_at(const asset_index *index, int position);
int asset_index_reference_count(const asset_index *index);
const asset_index_reference *asset_index_reference_at(const asset_index *index, int position);

/* Find References (Unreal): an asset, or a function graph or variable in it. */
typedef enum
{
    ASSET_INDEX_SYMBOL_ASSET,
    ASSET_INDEX_SYMBOL_FUNCTION,
    ASSET_INDEX_SYMBOL_VARIABLE,
} asset_index_symbol;

typedef struct
{
    int asset;
    asset_index_symbol symbol;
    uint64_t id; /* the function graph or variable */
} asset_index_target;

/* Appends every reference to target to *uses (support/array.h): nodes that
 * call, spawn, raise, handle, read or write it, and for an asset also the
 * attachments and bindings that name it. */
void asset_index_find_uses(
    const asset_index *index,
    asset_index_target target,
    const asset_index_reference ***uses
);

/* Rebuild the index from authored scene, prototype, graph and tab state. */
struct app_scene_authoring;
int app_scene_authoring_refresh_asset_index(struct app_scene_authoring *scene);
const asset_index *app_scene_authoring_asset_index(const struct app_scene_authoring *scene);
int app_scene_authoring_open_asset(struct app_scene_authoring *scene, int asset);

#endif
