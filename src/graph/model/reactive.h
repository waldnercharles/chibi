#ifndef CHIBI_BEHAVIOR_REACTIVE_MODEL_H
#define CHIBI_BEHAVIOR_REACTIVE_MODEL_H

#include "ecs/ecs.h"
#include "graph/model/behavior_asset.h"

typedef struct
{
    ecs_component_id component;
    size_t offset;
} behavior_reactive_step;
typedef struct
{
    behavior_reactive_step *steps;
    type_field field;
    uint64_t owner, variable_id;
    bool self, valid;
} behavior_reactive_path;

int behavior_reactive_output_index(const node *n, uint64_t id);
bool behavior_reactive_path_compile(
    const graph *g,
    const node *n,
    int pin,
    behavior_reactive_path *path,
    int depth
);
bool behavior_reactive_dependencies(
    const behavior_asset *asset,
    const graph *g,
    const node *n,
    behavior_reactive_path **paths,
    int depth
);
/* Rebuilds derived reads and guards from authored Set inputs. */
bool behavior_reactive_refresh(behavior_asset *asset, char *error, size_t size);
bool behavior_reactive_validate(const behavior_asset *asset, char *error, size_t size);

typedef struct
{
    uint64_t id;
    char kind[48];
} behavior_reactive_anchor;
/* Owned array of native identities with stable semantic roles at one source span. */
behavior_reactive_anchor *behavior_reactive_anchors(const graph *, const behavior_reactive *);

#endif
