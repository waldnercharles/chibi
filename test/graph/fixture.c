#include "test/graph/fixture.h"

#include "authoring/application_catalog.h"
#include "authoring/graph_catalog.h"
#include "graph/document/serialize.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "support/asset_text.h"

#include <assert.h>
#include <stdio.h>

static authoring_graph_catalog *palette;
static const registry_pack **previous;

void graph_palette_open()
{
    assert(!palette);
    char error[512] = { 0 };
    native_capabilities *native = authoring_application_catalog_new(true, error, sizeof error);
    if (!native) {
        fprintf(stderr, "graph palette: %s\n", error);
    }
    assert(native);
    palette = authoring_graph_catalog_new(
        (authoring_graph_catalog_config){
            .native = native,
            .types = TYPE_PACK,
            .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT } },
        error,
        sizeof error
    );
    native_capabilities_free(native);
    if (!palette) {
        fprintf(stderr, "graph palette: %s\n", error);
    }
    assert(palette);
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    int count = 0;
    const registry_pack *const *packs = authoring_graph_catalog_packs(palette, &count);
    registry_set_packs(packs, count);
}

void graph_palette_close()
{
    registry_set_packs(previous, acount(previous));
    afree(previous);
    previous = NULL;
    authoring_graph_catalog_free(palette);
    palette = NULL;
}

const node_desc *graph_palette_node(const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    if (!desc) {
        fprintf(stderr, "graph palette: no node named %s\n", name);
    }
    assert(desc);
    return desc;
}

char *graph_asset_text(const behavior_asset *asset)
{
    av_doc *doc = av_doc_new();
    av_value *value = behavior_asset_to_value(doc, asset);
    av_doc_set_root(doc, value);
    char *text = value ? av_doc_write(doc) : NULL;
    av_doc_free(doc);
    return text;
}

behavior_asset *graph_asset_read(const char *text)
{
    char error[160];
    av_doc *doc = av_doc_read(text, error, sizeof error);
    if (!doc) {
        return NULL;
    }
    behavior_asset *asset = behavior_asset_from_value(av_doc_root(doc));
    av_doc_free(doc);
    return asset;
}
