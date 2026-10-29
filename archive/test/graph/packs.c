#include "test/graph/packs.h"

#include "graph/modeling/model_compile.h"
#include "graph/nodes/core_nodes.h"
#include "graph/shader/shader_graph.h"
#include "runtime/component_catalog.h"
#include "support/array.h"

#include <assert.h>

static const registry_pack **previous;
static bool installed;

void test_graph_packs_install()
{
    assert(!installed);
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    static const registry_pack *const packs[] = { &MODEL_PACK, &SHADER_PACK, &CORE_FLOW_PACK };
    registry_set_packs(packs, 3);
    installed = true;
}

void test_graph_packs_restore()
{
    assert(installed);
    registry_set_packs(previous, acount(previous));
    afree(previous);
    installed = false;
}

type_catalog_pack test_graph_types(int index)
{
    if (index) {
        return TYPE_PACK;
    }
    return (type_catalog_pack){ .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
}
