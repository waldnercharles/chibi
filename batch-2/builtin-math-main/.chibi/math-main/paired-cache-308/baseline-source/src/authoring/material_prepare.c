#include "authoring/material_prepare.h"

#include "assets/field_parameters.h"
#include "runtime/component_catalog.h"
#include "support/array.h"

bool authoring_prototype_parameters_fields(
    material_fields *fields,
    const prototype *prototype,
    char *error,
    size_t size
)
{
    if (!fields) {
        return true;
    }
    for (int i = 0; i < acount(prototype->components); i++) {
        const prototype_component *component = &prototype->components[i];
        if (component->desc->id != COMPONENT_MaterialTerrain.id) {
            continue;
        }
        const MaterialTerrain *terrain = component->data;
        if (!material_field_prepare_arguments(fields, terrain->field, terrain->parameters, error, size)) {
            return false;
        }
    }
    for (int i = 0; i < acount(prototype->child_prototypes); i++) {
        if (!authoring_prototype_parameters_fields(fields, prototype->child_prototypes[i], error, size)) {
            return false;
        }
    }
    return true;
}
