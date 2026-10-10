#ifndef CHIBI_RUNTIME_PROTOTYPES_H
#define CHIBI_RUNTIME_PROTOTYPES_H

#include "runtime/scenes/scene.h"

typedef struct game_prototypes game_prototypes;

game_prototypes *game_prototypes_new();
void game_prototypes_free(game_prototypes *);
/* Retains an immutable generation whose objects descend from its first, the
 * entity a spawn returns, and copies its registration name. Replacing a name
 * never modifies generations retained by pending spawns. No authoring
 * document, source compiler, or filesystem resolver is retained. */
int game_prototypes_register(game_prototypes *, const char *, game_scene *, char *error, size_t size);
int game_prototypes_remove(game_prototypes *, const char *);
/* Borrowed until replacement/removal/free; retain before asynchronous use. */
const game_scene *game_prototypes_find(const game_prototypes *, const char *);
int game_prototypes_count(const game_prototypes *);
const char *game_prototypes_name(const game_prototypes *, int);

#endif
