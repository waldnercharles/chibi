#ifndef CBLUEPRINT_APP_PROTOTYPE_STILL_H
#define CBLUEPRINT_APP_PROTOTYPE_STILL_H

#include "assets/prototype.h"
#include "runtime/engine.h"

typedef struct prototype_still prototype_still;
prototype_still *prototype_still_new(const game_prototype_registry *registry);
void prototype_still_free(prototype_still *still);
/* Returns 1 when rebuilt, 0 when unchanged, -1 on error. Never attaches behaviors. */
int prototype_still_update(prototype_still *still, const game_prototype *prototype, char *error, size_t size);
ecs_world *prototype_still_world(prototype_still *still);
engine_viewport *prototype_still_viewport(prototype_still *still);
ecs_entity prototype_still_entity(const prototype_still *still);

#endif
