#pragma once
#include "authoring/source_collection.h"
#include "runtime/material_field.h"
/* Compiles a bounded native graph source collection into supplied field and
 * shader/model catalogs. Graphs link the source libraries in `sources`, or
 * none when it is NULL. No preview world, entities or executable callbacks. */
bool authoring_native_graphs_prepare(
    const char *content,
    const authoring_sources *sources,
    material_fields *,
    char *error,
    size_t size
);
