#include "authoring/material_prepare.h"
#include "runtime/behaviors/entities.h"
#include "runtime/materials.h"

bool authoring_prototype_parameters(
    game_entities *entities,
    const game_prototype *prototype,
    char *error,
    size_t size
)
{
    game_materials *materials = game_entities_materials(entities);
    return authoring_prototype_parameters_fields(
        materials ? game_materials_fields(materials) : NULL,
        prototype,
        error,
        size
    );
}
