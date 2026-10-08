#ifndef CHIBI_GAME_PROTOTYPE_H
#define CHIBI_GAME_PROTOTYPE_H

#include "ecs/ecs.h"
#include "support/asset_value.h"

#include <stddef.h>

typedef const char *(*game_prototype_include_fn)(const char *name, void *user);

/* Borrowed typed rows matching the registered descriptor's native layout.
 * Adding a component copies its fields and retains managed strings. */
typedef struct
{
    ecs_component_id component;
    const void *data;
    size_t size;
} game_prototype_default;

typedef struct
{
    const ecs_component_desc *const *components;
    int component_count;
    game_prototype_include_fn include;
    void *include_user;
    const game_prototype_default *defaults;
    int default_count;
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
    /* Registry value is owned; descriptors, default rows and include context
     * remain borrowed for subsequent authoring operations. */
    game_prototype_registry registry_storage;
    const game_prototype_registry *registry;
    game_prototype_tag tag;
    game_prototype_component *components;
    /* Authoring-only values; never included when spawning runtime components. */
    game_prototype_component *detached_components;
    char **graphs;
    char **strings;
} game_prototype;

/* The component a document names by stable key or reflected name, or NULL. */
const ecs_component_desc *game_prototype_registry_find(
    const game_prototype_registry *registry,
    const char *name
);
game_prototype *game_prototype_load(
    const char *value,
    const game_prototype_registry *registry,
    char *error,
    size_t error_size
);
/* A prototype named `name` with no components or behaviors. */
game_prototype *game_prototype_new(const char *name);
game_prototype *game_prototype_clone(const game_prototype *prototype, char *error, size_t error_size);
const char *game_prototype_identity(const game_prototype *prototype);
const ecs_component_desc *game_prototype_tag_desc(const game_prototype *prototype);
game_prototype *game_prototype_capture_entity(
    const char *name,
    const ecs_world *world,
    ecs_entity entity,
    const game_prototype_registry *registry
);
void game_prototype_free(game_prototype *prototype);
char *game_prototype_save(const game_prototype *prototype);
av_value *game_prototype_to_value(const game_prototype *prototype);
game_prototype *game_prototype_load_value(
    av_value *value,
    const game_prototype_registry *registry,
    char *error,
    size_t error_size
);
av_value *game_prototype_components_value(const game_prototype *prototype);
/* Owned single-component native value, including reflected nested fields. */
av_value *game_prototype_entity_component_value(
    const ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *desc,
    const game_prototype_registry *registry
);
/* Sparse fields, full added components, and null removed components, using
 * the same semantics as capturing an entity. Caller owns the returned native value. */
av_value *game_prototype_overrides_value(const game_prototype *base, const game_prototype *current);
av_value *game_prototype_capture_overrides_value(
    const game_prototype *base,
    const ecs_world *world,
    ecs_entity entity
);
int game_prototype_apply_components_value(
    ecs_world *world,
    ecs_entity entity,
    av_value *components,
    const game_prototype_registry *registry,
    char ***owned_strings,
    char *error,
    size_t error_size
);
/* Free strings retained by game_prototype_apply_components_value after their
 * ECS component values are no longer used. */
void game_prototype_owned_strings_free(char **strings);
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
/* Apply one reflected native value field using the same conversion rules as loading a
 * prototype document. The value is borrowed for the duration of the call. */
int game_prototype_write_field_value(
    game_prototype *prototype,
    const char *component_name,
    const char *field_name,
    av_value *value,
    char *error,
    size_t error_size
);
/* Restores detached values, otherwise uses the first supplied typed default
 * row for this component, or zero initialization when the registry omits it. */
int game_prototype_add_component(game_prototype *prototype, const ecs_component_desc *desc);
int game_prototype_remove_component(game_prototype *prototype, const char *component_name);
/* Detach retains values for add_component to restore, including after save/load. */
int game_prototype_detach_component(game_prototype *prototype, const char *component_name);
/* Single-component objects from entity_component_value. Defaults may be {} when
 * the component is local or no longer in the prototype. The owned result keeps
 * local edits and adopts current defaults for unchanged fields, recursively. */
av_value *game_prototype_rebase_component_value(
    av_value *values,
    av_value *previous_defaults,
    av_value *current_defaults
);
/* Owned single-component object; {} if this prototype has no such component. */
av_value *game_prototype_component_value(const game_prototype *prototype, const char *name);
/* Owned default component values shared by prototype and scene attachment. */
av_value *game_prototype_component_defaults_value(
    const game_prototype_registry *registry,
    const ecs_component_desc *desc
);
int game_prototype_add_graph(game_prototype *prototype, const char *asset_name);
int game_prototype_remove_graph(game_prototype *prototype, const char *asset_name);
int game_prototype_has_graph(const game_prototype *prototype, const char *asset_name);

#endif
