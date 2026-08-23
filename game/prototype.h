#ifndef CBLUEPRINT_GAME_PROTOTYPE_H
#define CBLUEPRINT_GAME_PROTOTYPE_H

#include "../ecs/ecs.h"

#include <stddef.h>

typedef struct game_prototype game_prototype;

typedef const char *(*game_prototype_include_fn)(const char *name, void *user);

typedef struct
{
    const ecs_component_desc *const *components;
    int component_count;
    game_prototype_include_fn include;
    void *include_user;
} game_prototype_registry;

typedef int (*game_prototype_attach_fn)(
    ecs_world *world,
    ecs_entity entity,
    const char *graph_name,
    void *user
);

typedef struct
{
    game_prototype_attach_fn attach;
    void *attach_user;
} game_prototype_spawn_hooks;

game_prototype *game_prototype_load(
    const char *json,
    const game_prototype_registry *registry,
    char *error,
    size_t error_size
);
void game_prototype_free(game_prototype *prototype);
char *game_prototype_save(const game_prototype *prototype);
int game_prototype_spawn(
    const game_prototype *prototype,
    ecs_world *world,
    const game_prototype_spawn_hooks *hooks,
    ecs_entity *out_entity,
    char *error,
    size_t error_size
);

const char *game_prototype_name(const game_prototype *prototype);
int game_prototype_component_count(const game_prototype *prototype);
const ecs_component_desc *game_prototype_component(const game_prototype *prototype, int index);
const void *game_prototype_component_data(const game_prototype *prototype, int index);
int game_prototype_graph_count(const game_prototype *prototype);
const char *game_prototype_graph(const game_prototype *prototype, int index);

#endif
