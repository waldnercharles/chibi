#include "authoring/material_prepare.h"
#include "authoring/prototype_prepare.h"
#include "runtime/behaviors/entities.h"

int entities_register(entities *entities, const prototype *prototype)
{
    char error[256];
    if (!authoring_prototype_parameters(entities, prototype, error, sizeof error)) {
        return -1;
    }
    scene *prepared = authoring_prototype_prepare(prototype, error, sizeof error);
    if (!prepared) {
        return -1;
    }
    int result = entities_register_prepared(entities, prototype->identity, prepared);
    scene_free(prepared);
    return result;
}
