#ifndef CHIBI_GAME_PROTOTYPE_H
#define CHIBI_GAME_PROTOTYPE_H

#include "ecs/ecs.h"

#include <stddef.h>

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

typedef struct
{
    const ecs_component_desc *desc;
    void *data;
} game_prototype_component;

typedef struct
{
    ecs_component_desc desc;
    char *name;
} game_prototype_tag;

typedef struct game_prototype
{
    char *name;
    char *identity;
    const game_prototype_registry *registry;
    game_prototype_tag tag;
    game_prototype_component *components;
    /* Authoring-only values; never included when spawning runtime components. */
    game_prototype_component *detached_components;
    char **graphs;
    char **strings;
} game_prototype;

game_prototype *game_prototype_load(
    const char *json,
    const game_prototype_registry *registry,
    char *error,
    size_t error_size
);
game_prototype *game_prototype_clone(const game_prototype *prototype, char *error, size_t error_size);
game_prototype *game_prototype_duplicate(const game_prototype *prototype, char *error, size_t error_size);
int game_prototype_set_name(game_prototype *prototype, const char *name);
const char *game_prototype_identity(const game_prototype *prototype);
const ecs_component_desc *game_prototype_tag_desc(const game_prototype *prototype);
int game_prototype_register_tag(const game_prototype *prototype, ecs_world *world);
int game_prototype_matches(const game_prototype *prototype, const ecs_world *world, ecs_entity entity);
game_prototype *game_prototype_capture_entity(
    const char *name,
    const ecs_world *world,
    ecs_entity entity,
    const game_prototype_registry *registry
);
void game_prototype_free(game_prototype *prototype);
char *game_prototype_save(const game_prototype *prototype);
char *game_prototype_components_json(const game_prototype *prototype);
/* Owned single-component JSON, including reflected nested fields. */
char *game_prototype_entity_component_json(
    const ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *desc,
    const game_prototype_registry *registry
);
/* Sparse fields, full added components, and null removed components, using
 * the same semantics as capturing an entity. Caller owns the returned JSON. */
char *game_prototype_overrides_json(const game_prototype *base, const game_prototype *current);
char *game_prototype_capture_overrides_json(
    const game_prototype *base,
    const ecs_world *world,
    ecs_entity entity
);
int game_prototype_apply_components_json(
    ecs_world *world,
    ecs_entity entity,
    const char *components_json,
    const game_prototype_registry *registry,
    char *error,
    size_t error_size
);
/* Allocate authored components without running attached behaviors. */
int game_prototype_spawn_components(
    const game_prototype *prototype,
    ecs_world *world,
    ecs_entity *entity,
    char *error,
    size_t error_size
);
int game_prototype_spawn(
    const game_prototype *prototype,
    ecs_world *world,
    const game_prototype_spawn_hooks *hooks,
    ecs_entity *out_entity,
    char *error,
    size_t error_size
);

int game_prototype_read_field(
    const game_prototype *prototype,
    const char *component_name,
    const char *field_name,
    pin_value *out
);
int game_prototype_write_field(
    game_prototype *prototype,
    const char *component_name,
    const char *field_name,
    pin_value value
);
int game_prototype_add_component(game_prototype *prototype, const ecs_component_desc *desc);
int game_prototype_remove_component(game_prototype *prototype, const char *component_name);
/* Detach retains values for add_component to restore, including after save/load. */
int game_prototype_detach_component(game_prototype *prototype, const char *component_name);
/* Single-component objects from entity_component_json. Defaults may be {} when
 * the component is local or no longer in the prototype. The owned result keeps
 * local edits and adopts current defaults for unchanged fields, recursively. */
char *game_prototype_rebase_component_json(
    const char *values,
    const char *previous_defaults,
    const char *current_defaults
);
/* Owned single-component object; {} if this prototype has no such component. */
char *game_prototype_component_json(const game_prototype *prototype, const char *name);
/* Owned default component values shared by prototype and scene attachment. */
char *game_prototype_component_defaults_json(
    const game_prototype_registry *registry,
    const ecs_component_desc *desc
);
int game_prototype_add_graph(game_prototype *prototype, const char *asset_name);
int game_prototype_remove_graph(game_prototype *prototype, const char *asset_name);
int game_prototype_has_graph(const game_prototype *prototype, const char *asset_name);

#endif
