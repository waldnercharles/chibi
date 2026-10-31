#pragma once
#include "authoring/source_collection.h"
#include "runtime/material_field.h"
/* Compiles a bounded native graph source collection into supplied field and
 * shader/model catalogs. Graphs link the source libraries of `code`, or with
 * no code, those of the checked `declarations` snapshot, analyzed only when
 * the content has native graphs. No preview world, entities or executable
 * callbacks. */
bool authoring_native_graphs_prepare(
    const char *content,
    const authoring_sources *code,
    const source_catalog *declarations,
    material_fields *,
    char *error,
    size_t size
);
