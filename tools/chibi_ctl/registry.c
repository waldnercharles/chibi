#include "runtime/component_catalog.h"
#include <assert.h>

int type_catalog_pack_count()
{
    return 1;
}
type_catalog_pack type_catalog_pack_at(int index)
{
    assert(index == 0);
    (void)index;
    return TYPE_PACK;
}
