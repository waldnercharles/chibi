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
/* The documents, and their bytes, that loads retained together have read. */
typedef struct
{
    int documents;
    size_t bytes;
} project_expansion;
/* Adds what loading `prototype` read to `expansion`; false once it reaches
 * GAME_PROTOTYPE_EXPANSION_MAX documents or GAME_PROTOTYPE_READ_MAX bytes. */
bool project_prepare_expand(
    project_expansion *expansion,
    const game_prototype *prototype,
    char *error,
    size_t size
);
void project_prepare_prototypes_free(project_prototype *);
