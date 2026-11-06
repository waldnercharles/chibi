#ifndef CHIBI_BEHAVIOR_ASSET_H
#define CHIBI_BEHAVIOR_ASSET_H

#include "graph/model/graph.h"
#include "graph/model/node.h"

#include <stdint.h>

/* CHIBI-1 / CHIBI-8 / CHIBI-12: an authored Behavior Asset definition owns its ID counter
 * (kept on its single default event graph), metadata, optional nominal owner
 * handle type, and variable list. Runtime state never lives here. */
typedef struct behavior_variable
{
    uint64_t id;
    char *name;
    pin_type type;
    char *type_name;
    pin_container container;
    pin_value default_value; /* authored default; strings are graph-owned */
    char *category;
    bool shared; /* System registration storage; false is entity-private state. */
} behavior_variable;

typedef enum
{
    BEHAVIOR_REACTIVE_BIND,
    BEHAVIOR_REACTIVE_PRESSED_CHANGED,
} behavior_reactive_kind;

/* Executable relationship roots. Node/pin IDs address native graph semantics;
 * optional authoring metadata never defines a binding or subscription. */
typedef struct
{
    behavior_reactive_kind kind;
    uint64_t graph_id, event_id, source_node_id, source_pin_id, statement_node_id;
    uint64_t value_node_id, value_pin_id;
} behavior_reactive;

typedef enum
{
    BEHAVIOR_DOMAIN_EVENT,
    BEHAVIOR_DOMAIN_LIBRARY,
    BEHAVIOR_DOMAIN_MATERIAL,
    BEHAVIOR_DOMAIN_SHAPE,
    BEHAVIOR_DOMAIN_EVENT_DEFINITION,
    BEHAVIOR_DOMAIN_SHADER_LIBRARY,
    BEHAVIOR_DOMAIN_FIELD,
    BEHAVIOR_DOMAIN_MODEL,
} behavior_domain;

/* A locked authoring reference, in model units. It never affects geometry. */
typedef struct
{
    char *image;                  /* Content-relative image key, graph-owned. */
    float center[2], span, angle; /* Image height, radians; aspect preserved. */
    bool locked;
} model_reference;

typedef struct
{
    char *name, *key; /* Graph-owned executable system parameter declaration. */
    bool optional;
} behavior_component_parameter;

typedef struct behavior_asset
{
    void *artifact; /* Optional compiler-owned cache, retired with this document. */
    void (*artifact_free)(void *);
    uint64_t identity; /* Process-local identity for ownership and prepared execution guards. */
    behavior_domain domain;
    /* In-memory source lowering: function and parameter IDs are not durable. */
    bool transient_function_ids;
    model_reference reference;
    float bounds[2];       /* Shape half extents in local units. */
    graph *graph;          /* the one non-deletable default event graph */
    struct graph **graphs; /* includes graph; graph 0 is always the default */
    char *name;            /* graph-owned */
    char *description;     /* graph-owned */
    char *owner_type_name; /* optional nominal PIN_HANDLE owner type */
    behavior_variable *variables;
    behavior_component_parameter *component_parameters;
    behavior_reactive *reactives;
} behavior_asset;

const char *behavior_domain_name(behavior_domain domain);
/* Executable behavior/library assets use code; declarative graph domains use KDL. */
const char *behavior_asset_extension(behavior_domain domain);
/* Materials, shapes, shader libraries and fields compute values only: their
 * graphs have no execution pins, so their functions are pure. */
bool behavior_domain_pure(behavior_domain domain);

/* Create a behavior_asset with one empty default event graph. `name` is copied. */
behavior_asset *behavior_asset_new(const char *name);

behavior_component_parameter *behavior_asset_add_component_parameter(
    behavior_asset *,
    const char *name,
    const char *key,
    bool optional
);

/* CHIBI-2..CHIBI-4: named graphs share the Behavior Asset ID namespace. The default
 * event graph is created by behavior_asset_new() and cannot be deleted. */
graph *behavior_asset_add_graph(behavior_asset *asset, graph_kind kind, const char *name);

graph *behavior_asset_find_graph(const behavior_asset *asset, uint64_t graph_id);
graph *behavior_asset_find_graph_by_name(const behavior_asset *asset, const char *name);

int behavior_asset_delete_graph(behavior_asset *asset, uint64_t graph_id);
void behavior_asset_free(behavior_asset *asset);
/* Replaces and retires the single owned compiler artifact. NULL clears it.
 * The model never interprets the payload or invokes compilation. */
void behavior_asset_set_artifact(behavior_asset *, void *, void (*release)(void *));
void *behavior_asset_artifact(const behavior_asset *);

/* The definition is mutable only while no runtime instance is attached and no
 * legacy flow_run holds the graph guard (graph->flow_pause_count == 0). */
int behavior_asset_mutable(const behavior_asset *asset);

int behavior_asset_set_name(behavior_asset *asset, const char *name);
void behavior_asset_set_description(behavior_asset *asset, const char *description);
bool behavior_asset_set_model_reference(behavior_asset *asset, const model_reference *reference);
int behavior_asset_set_owner_type(behavior_asset *asset, const char *type_name);

/* Variables. Names are unique within the behavior_asset (VAR-3). Default
 * strings are copied into behavior_asset-owned storage. */
behavior_variable *behavior_asset_add_variable(
    behavior_asset *asset,
    const char *name,
    pin_type type,
    const char *type_name,
    pin_container container,
    const pin_value *default_value,
    const char *category
);
behavior_variable *behavior_asset_find_variable_by_name(const behavior_asset *asset, const char *name);
behavior_variable *behavior_asset_find_variable_by_id(const behavior_asset *asset, uint64_t id);

int behavior_asset_set_variable_category(behavior_asset *asset, uint64_t id, const char *category);

int behavior_asset_delete_variable(behavior_asset *asset, uint64_t id);
int behavior_asset_set_variable_default(behavior_asset *asset, uint64_t id, const pin_value *value);
/* A new variable named `name` with the type, default and category of `id`. */
behavior_variable *behavior_asset_duplicate_variable(
    behavior_asset *asset,
    uint64_t id,
    const char *name
);

#endif
