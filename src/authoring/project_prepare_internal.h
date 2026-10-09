#pragma once
#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "authoring/project_prepare.h"

typedef struct
{
    char *name, *text;
    game_prototype *prototype;
} project_prototype;
char *project_prepare_read(const char *, size_t *budget, char *, size_t);
bool project_prepare_prototypes(
    const char *,
    game_prototype_registry *,
    project_prototype **,
    char *,
    size_t
);
const game_prototype *project_prepare_prototype(const project_prototype *, const char *);
game_prototype *project_prepare_object(
    const game_scene_entity *,
    const project_prototype *,
    char *,
    size_t
);
/* Adds what loading `prototype` read to `expanded`, the documents read so far
 * by loads retained together; false once it reaches
 * GAME_PROTOTYPE_EXPANSION_MAX. */
bool project_prepare_expand(int *expanded, const game_prototype *prototype, char *error, size_t size);
void project_prepare_prototypes_free(project_prototype *);
