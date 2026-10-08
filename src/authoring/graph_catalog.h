#ifndef CHIBI_AUTHORING_GRAPH_CATALOG_H
#define CHIBI_AUTHORING_GRAPH_CATALOG_H
#include "ecs/component_catalog.h"
#include "graph/model/registry.h"
#include "runtime/native_capabilities.h"

typedef struct authoring_graph_catalog authoring_graph_catalog;
typedef struct
{
    native_capabilities *native;
    type_catalog_pack types;
    ecs_component_catalog components;
    const registry_pack *const *extra_packs;
    int extra_count;
} authoring_graph_catalog_config;
/* Owns its neutral catalog reference and metadata descriptors. Static extra
 * packs, reflected types and component descriptors remain borrowed. Construction
 * does not change registry composition. No behavioral graph callbacks are made. */
authoring_graph_catalog *authoring_graph_catalog_new(
    authoring_graph_catalog_config,
    char *error,
    size_t size
);
const registry_pack *const *authoring_graph_catalog_packs(const authoring_graph_catalog *, int *count);
int authoring_graph_catalog_type_pack_count(const authoring_graph_catalog *);
type_catalog_pack authoring_graph_catalog_type_pack_at(const authoring_graph_catalog *, int index);
/* Retire every graph/registry reference before releasing the catalog. */
void authoring_graph_catalog_free(authoring_graph_catalog *);
#endif
