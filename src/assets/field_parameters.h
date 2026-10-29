#pragma once
#include "runtime/material_field.h"

/* KDL parameter text is asset preparation, separate from the typed runtime
 * catalog. Each call validates names/types, then passes scalar records to it. */
bool material_field_bake_text(
    const material_fields *,
    const char *name,
    const char *parameters,
    unsigned seed,
    field_grid,
    float *out,
    const task_system *,
    char *error,
    size_t size
);
bool material_field_bake_rows_text(
    const material_fields *,
    const char *name,
    const char *parameters,
    unsigned seed,
    field_grid,
    int first,
    int count,
    float *out,
    const task_system *,
    char *error,
    size_t size
);

/* Prepare an opaque argument key for scene/prototype publication. Runtime
 * terrain lookup subsequently consumes only the copied typed record. */
bool material_field_prepare_arguments(
    material_fields *,
    const char *field,
    const char *text,
    char *error,
    size_t size
);
