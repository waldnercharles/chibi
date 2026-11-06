#pragma once
#include "graph/model/behavior_asset.h"
#include "runtime/material_field.h"
/* Compiles a bounded native graph source collection into supplied field and
 * shader/model catalogs. Borrows the source libraries' native graph
 * projections. No preview world, entities or executable callbacks. */
bool authoring_native_graphs_prepare(
    const char *content,
    const behavior_asset *const *libraries,
    int library_count,
    material_fields *,
    char *error,
    size_t size
);
