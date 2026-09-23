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
    char *error,
    size_t size
);
