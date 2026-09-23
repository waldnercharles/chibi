#pragma once
#include "graph/model/behavior_asset.h"
#include "third_party/yyjson/yyjson.h"
bool automation_field_preview(
    const behavior_asset *asset,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t size
);
