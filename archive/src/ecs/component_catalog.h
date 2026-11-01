#ifndef CHIBI_ECS_COMPONENT_CATALOG_H
#define CHIBI_ECS_COMPONENT_CATALOG_H

#include "ecs/ecs.h"

/* Descriptor storage remains owned by the supplying component pack. Lookup
 * preserves pack order and accepts either a stable key or reflected name. */
typedef struct
{
    const ecs_component_desc *const *components;
    int component_count;
} ecs_component_catalog;

const ecs_component_desc *ecs_component_catalog_find(ecs_component_catalog, const char *name);
/* Stable identity for a runtime marker's nonempty immutable key. */
ecs_component_id ecs_component_marker_id(const char *key);

#endif
