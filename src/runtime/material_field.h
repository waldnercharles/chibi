#pragma once
#include "graph/compiler/field_program.h"

typedef struct material_fields material_fields;
material_fields *material_fields_new();
void material_fields_free(material_fields *fields);
/* Compiles atomically; a rejected edit retains the last valid program. */
bool material_field_compile(material_fields *fields, const behavior_asset *asset, char *error, size_t size);
unsigned material_field_revision(const material_fields *fields, const char *name);
bool material_field_bake(
    const material_fields *fields,
    const char *name,
    const char *parameters,
    unsigned seed,
    field_grid grid,
    float *out,
    const game_task_system *tasks,
    char *error,
    size_t size
);
/* One value of the field at (x, y), as a bake's cell centered there. Parsed
 * parameter objects are cached per field. */
bool material_field_sample(
    material_fields *fields,
    const char *name,
    const char *parameters,
    unsigned seed,
    float x,
    float y,
    float *out,
    char *error,
    size_t size
);
