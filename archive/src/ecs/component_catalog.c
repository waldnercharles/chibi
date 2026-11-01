#include "ecs/component_catalog.h"

#include <assert.h>
#include <string.h>

ecs_component_id ecs_component_marker_id(const char *key)
{
    assert(key && *key);
    uint64_t hash = UINT64_C(1469598103934665603);
    for (const unsigned char *p = (const unsigned char *)key; *p; p++) {
        hash ^= *p;
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1;
}

const ecs_component_desc *ecs_component_catalog_find(ecs_component_catalog catalog, const char *name)
{
    assert(catalog.component_count >= 0 && name);
    assert(!catalog.component_count || catalog.components);
    for (int i = 0; i < catalog.component_count; i++) {
        const ecs_component_desc *desc = catalog.components[i];
        if (desc && (!strcmp(desc->name, name) ||
                     (desc->key && !strcmp(desc->key, name)))) {
            return desc;
        }
    }
    return NULL;
}
