#ifndef CHIBI_RUNTIME_PROTOTYPES_H
#define CHIBI_RUNTIME_PROTOTYPES_H

#include "runtime/scenes/scene.h"

typedef struct prototypes prototypes;

prototypes *prototypes_new();
void prototypes_free(prototypes *);
/* Retains an immutable generation whose objects descend from its first, the
 * entity a spawn returns, and copies its registration name. Replacing a name
 * never modifies generations retained by pending spawns. No authoring
 * document, source compiler, or filesystem resolver is retained. */
int prototypes_register(prototypes *, const char *, scene *, char *error, size_t size);
int prototypes_remove(prototypes *, const char *);
/* Borrowed until replacement/removal/free; retain before asynchronous use. */
const scene *prototypes_find(const prototypes *, const char *);
int prototypes_count(const prototypes *);
const char *prototypes_name(const prototypes *, int);

#endif
