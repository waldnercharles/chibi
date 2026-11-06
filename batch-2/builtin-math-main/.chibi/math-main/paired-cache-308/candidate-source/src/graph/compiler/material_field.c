#include "graph/compiler/material_field.h"

bool material_field_compile(material_fields *fields, const behavior_asset *asset, char *error, size_t size)
{
    field_program *program = field_program_compile(asset, error, size);
    return program && material_field_register(fields, asset->name, program);
}
