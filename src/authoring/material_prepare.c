#include "authoring/material_prepare.h"

#include "assets/field_parameters.h"
#include "runtime/component_catalog.h"
#include "support/array.h"

bool authoring_prototype_parameters_fields(
    material_fields *fields,
    const game_prototype *prototype,
    char *error,
    size_t size
)
{
    if (!fields) {
        return true;
    }
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        if (component->desc->id != GAME_COMPONENT_MaterialTerrain.id) {
            continue;
        }
        const MaterialTerrain *terrain = component->data;
        if (!material_field_prepare_arguments(fields, terrain->field, terrain->parameters, error, size)) {
            return false;
        }
    }
    return true;
}
