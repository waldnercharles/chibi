#include <stdbool.h>
#include "registry.h"
#include "authoring/application_catalog.h"
#include "authoring/graph_catalog.h"
#include "graph/nodes/studio_nodes.h"
#include "runtime/component_catalog.h"
#include <assert.h>
#include <stdio.h>

static authoring_graph_catalog *catalog;
int type_catalog_pack_count()
{
    return catalog ? authoring_graph_catalog_type_pack_count(catalog) : 1;
}
type_catalog_pack type_catalog_pack_at(int index)
{
    if (catalog) return authoring_graph_catalog_type_pack_at(catalog, index);
    assert(index == 0);
    return GAME_TYPE_PACK;
}
bool chibi_ctl_registry_init()
{
    assert(!catalog);
    char error[512] = { 0 };
    native_capabilities *native = authoring_application_catalog_new(true, error, sizeof error);
    const registry_pack *extra[] = { &STUDIO_PACK };
    if (native) {
        catalog = authoring_graph_catalog_new((authoring_graph_catalog_config){
            .native = native, .types = GAME_TYPE_PACK,
            .components = { GAME_REGISTRY_COMPONENTS, GAME_REGISTRY_COMPONENT_COUNT },
            .extra_packs = extra, .extra_count = 1 }, error, sizeof error);
        native_capabilities_free(native);
    }
    if (!catalog) {
        fprintf(stderr, "chibi_ctl catalog: %s\n", error);
        return false;
    }
    int count;
    const registry_pack *const *packs = authoring_graph_catalog_packs(catalog, &count);
    registry_set_packs(packs, count);
    return true;
}
void chibi_ctl_registry_free()
{
    registry_set_packs(NULL, 0);
    authoring_graph_catalog_free(catalog);
    catalog = NULL;
}
