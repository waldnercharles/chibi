#ifndef CHIBI_RUNTIME_SCENE_H
#define CHIBI_RUNTIME_SCENE_H

#include "runtime/behaviors/source.h"
#include "runtime/scenes/physics_settings.h"

typedef struct game_scene game_scene;
typedef struct game_scene_instance game_scene_instance;
typedef struct
{
    const ecs_component_desc *desc;
    const void *data;
} game_scene_component;
typedef struct
{
    const char *name;
    const game_scene_component *components;
    int component_count;
    const char *const *behaviors;
    int behavior_count;
} game_scene_object;
/* An object's handle field that names another object of the scene: each
 * instance writes the target's entity there. `component` indexes the object's
 * components and `field` that component type's top-level fields. */
typedef struct
{
    int object, component, field, target;
} game_scene_link;
typedef struct
{
    const char *name, *ui;
    bool perspective;
    game_physics_settings physics;
    const game_scene_object *objects;
    int object_count;
    /* Each object's parent, an earlier object's index or -1 for a root; NULL
     * when every object is a root. Names are unique among siblings. */
    const int *parents;
    const game_scene_link *links;
    int link_count;
} game_scene_spec;

/* Snapshots all values/names. Reflected type descriptors remain host-owned and
 * must outlive the scene and worlds using its registered component layouts.
 * No source, graph, editor state, or filesystem resolver is retained. */
game_scene *game_scene_prepare(const game_scene_spec *, char *error, size_t size);
void game_scene_retain(game_scene *);
void game_scene_free(game_scene *);
const char *game_scene_name(const game_scene *);
const char *game_scene_ui(const game_scene *);
bool game_scene_perspective(const game_scene *);
game_physics_settings game_scene_physics(const game_scene *);
int game_scene_object_count(const game_scene *);
const char *game_scene_object_name(const game_scene *, int);
/* The object's parent index, or -1 for a root. */
int game_scene_object_parent(const game_scene *, int);
/* Whether every object descends from the first, as a prototype's do. */
bool game_scene_is_tree(const game_scene *);
int game_scene_link_count(const game_scene *);
game_scene_link game_scene_link_at(const game_scene *, int);
int game_scene_object_behavior_count(const game_scene *, int);
const char *game_scene_object_behavior(const game_scene *, int object, int behavior);
/* Retained arena capacity plus the owning generation, for package budgets. */
size_t game_scene_storage_bytes(const game_scene *);
/* Borrowed immutable component payloads/descriptors until scene release. */
int game_scene_object_component_count(const game_scene *, int object);
game_scene_component game_scene_object_component_at(const game_scene *, int object, int component);
/* Explicit schema installation, outside ECS iteration and pending commands. */
int game_scene_register_components(const game_scene *, ecs_world *, char *error, size_t size);
/* Components must already be installed. Takes a scene reference and borrows
 * world/registry until free. Each child is ChildOf its parent's entity, and
 * each link holds its target's. All entities/components exist before any
 * behavior is attached; Begin remains deferred until the host's
 * initialization boundary. Failure removes created entities and cancels their
 * attachments. */
game_scene_instance *game_scene_instantiate(
    game_scene *,
    ecs_world *,
    game_source_registry *,
    char *error,
    size_t size
);
/* Allocate component snapshots without attaching behavior modules. The caller
 * may initialize them and attach behavior at its normal initialization boundary. */
game_scene_instance *game_scene_instantiate_components(game_scene *, ecs_world *, char *error, size_t size);
/* Consume a component-only instance whose objects all descend from the first,
 * transferring entity teardown to the caller: destroying the first entity
 * destroys the rest. Returns the entities in object order; the caller afrees
 * the array. Their managed components remain owned by the ECS world. */
ecs_entity *game_scene_instance_take_entities(game_scene_instance *);
void game_scene_instance_free(game_scene_instance *);
ecs_entity game_scene_instance_entity(const game_scene_instance *, int);
/* Borrowed immutable scene retained by the instance. */
const game_scene *game_scene_instance_scene(const game_scene_instance *);

#endif
