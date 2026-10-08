#pragma once
#include "runtime/material_field.h"
/* Compiles a bounded native graph source collection into supplied field and
 * shader/model catalogs. No preview world, entities or executable callbacks. */
bool authoring_native_graphs_prepare(const char *content, material_fields *, char *error, size_t size);
