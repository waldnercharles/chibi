#ifndef CHIBI_REFERENCE_ADAPTERS_H
#define CHIBI_REFERENCE_ADAPTERS_H

#include "authoring/reference_index.h"
#include "graph/model/behavior_asset.h"
#include "support/asset_value.h"

struct prototype;

/* The kind of asset a graph asset of this domain is. */
reference_asset_kind reference_domain_kind(behavior_domain domain);
/* Graph content: definitions of functions, macros, parameters, variables,
 * locals and an event definition's event; calls and their arguments;
 * variable, component and field reads and writes, through nodes and pin
 * bindings; locals and parameters read; events sent and handled; prototypes
 * spawned; and assets pin defaults name, as a Shader value's material. */
void reference_index_add_graphs(reference_index *index, int source, const behavior_asset *asset);
/* Its behaviors, the assets and events its component fields name, and each
 * child as a reference_object named by its path among the children. */
void reference_index_add_prototype(reference_index *index, int source, const struct prototype *prototype);

/* A scene object: its prototype, behaviors, and the assets its component
 * fields name. With base, the object also holds what base's fields name,
 * unless its overrides set those fields. */
typedef struct
{
    const char *name; /* its path in the scene */
    const char *prototype;
    const struct prototype *base; /* may be NULL */
    av_value *overrides; /* borrowed component overrides; may be NULL */
    const char *const *behaviors;
    int behavior_count;
} reference_object;
void reference_index_add_object(reference_index *index, int source, const reference_object *object);
/* A ui document's widgets (<use ui>) and the events it sends (emit('Name'),
 * data-keys). */
void reference_index_add_ui(reference_index *index, int source, const char *rml);

#endif
