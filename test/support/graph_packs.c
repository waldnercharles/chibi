#include "test/support/graph_packs.h"

#include "authoring/graph_catalog.h"
#include "host/native_default.h"
#include "runtime/component_catalog.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>

static authoring_graph_catalog *catalog;
static const registry_pack **previous;

bool test_graph_packs_set()
{
    assert(!catalog);
    char error[512] = "";
    native_capabilities *native = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    if (native) {
        catalog = authoring_graph_catalog_new(
            (authoring_graph_catalog_config){
                .native = native,
                .types = TYPE_PACK,
                .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT } },
            error,
            sizeof error
        );
        native_capabilities_free(native);
    }
    if (!catalog) {
        fprintf(stderr, "game graph packs: %s\n", error);
        return false;
    }
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    int count = 0;
    const registry_pack *const *packs = authoring_graph_catalog_packs(catalog, &count);
    registry_set_packs(packs, count);
    return true;
}

void test_graph_packs_clear()
{
    if (!catalog) {
        return;
    }
    registry_set_packs(previous, acount(previous));
    afree(previous);
    previous = NULL;
    authoring_graph_catalog_free(catalog);
    catalog = NULL;
}
