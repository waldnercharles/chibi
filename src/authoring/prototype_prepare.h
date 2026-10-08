#ifndef CHIBI_AUTHORING_PROTOTYPE_PREPARE_H
#define CHIBI_AUTHORING_PROTOTYPE_PREPARE_H

#include "assets/prototype.h"
#include "runtime/scenes/scene.h"

typedef struct game_entities game_entities;
/* Snapshot resolved authoring data; the caller owns the prepared generation. */
bool authoring_prototype_resources(const game_prototype *, char *error, size_t size);
game_scene *authoring_prototype_prepare(const game_prototype *, char *error, size_t size);
int game_entities_register(game_entities *, const game_prototype *);

#endif
