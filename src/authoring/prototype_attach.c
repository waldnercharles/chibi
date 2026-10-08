#include "authoring/material_prepare.h"
#include "authoring/prototype_prepare.h"
#include "runtime/behaviors/entities.h"

int game_entities_register(game_entities *entities, const game_prototype *prototype)
{
    char error[256];
    if (!authoring_prototype_parameters(entities, prototype, error, sizeof error)) {
        return -1;
    }
    game_scene *prepared = authoring_prototype_prepare(prototype, error, sizeof error);
    if (!prepared) {
        return -1;
    }
    int result = game_entities_register_prepared(entities, prototype->identity, prepared);
    game_scene_free(prepared);
    return result;
}
