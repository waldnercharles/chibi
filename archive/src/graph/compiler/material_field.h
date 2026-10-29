#pragma once
#include "graph/compiler/field_program.h"
#include "runtime/material_field.h"
/* Graph compilation belongs to authoring. A rejected graph retains the last
 * registered runtime generation. */
bool material_field_compile(material_fields *, const behavior_asset *, char *error, size_t size);
