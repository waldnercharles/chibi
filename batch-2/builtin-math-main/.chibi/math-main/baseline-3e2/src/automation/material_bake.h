#ifndef CHIBI_AUTOMATION_MATERIAL_BAKE_H
#define CHIBI_AUTOMATION_MATERIAL_BAKE_H
#include "graph/model/behavior_asset.h"

#include <yyjson.h>
bool automation_material_bake(
    const behavior_asset *,
    yyjson_val *,
    yyjson_mut_doc *,
    yyjson_mut_val *,
    char *,
    size_t
);
#endif
