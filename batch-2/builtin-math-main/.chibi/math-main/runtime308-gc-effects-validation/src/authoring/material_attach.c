#include "authoring/material_prepare.h"
#include "runtime/behaviors/entities.h"
#include "runtime/materials.h"

bool authoring_prototype_parameters(entities *entities, const prototype *prototype, char *error, size_t size)
{
    materials *materials = entities_materials(entities);
    return authoring_prototype_parameters_fields(
        materials ? materials_fields(materials) : NULL,
        prototype,
        error,
        size
    );
}
