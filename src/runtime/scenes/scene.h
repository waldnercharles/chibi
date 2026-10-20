#ifndef CHIBI_RUNTIME_SCENE_H
#define CHIBI_RUNTIME_SCENE_H

#include "runtime/behaviors/source.h"
#include "runtime/scenes/physics_settings.h"

typedef struct scene scene;
typedef struct scene_instance scene_instance;
typedef struct
{
    const ecs_component_desc *desc;
    const void *data;
} scene_component;
typedef struct
{
    const char *name;
    const scene_component *components;
    int component_count;
    const char *const *behaviors;
    int behavior_count;
} scene_object;
/* An object's handle field that names another object of the scene: each
 * instance writes the target's entity there. `component` indexes the object's
 * components and `field` that component type's top-level fields. */
typedef struct
{
    int object, component, field, target;
} scene_link;
typedef struct
{
    int object, relationship, target;
} scene_pair;
typedef struct
{
    const char *name, *ui;
    bool perspective;
    physics_settings physics;
    const scene_object *objects;
    int object_count;
    /* Each object's parent, an earlier object's index or -1 for a root; NULL
     * when every object is a root. Names are unique among siblings. */
    const int *parents;
    const scene_link *links;
    int link_count;
    const ecs_relationship_desc *relationships;
    int relationship_count;
    const scene_pair *pairs;
    int pair_count;
} scene_spec;

/* Snapshots all values/names. Reflected type descriptors remain host-owned and
 * must outlive the scene and worlds using its registered component layouts.
 * No source, graph, editor state, or filesystem resolver is retained. */
scene *scene_prepare(const scene_spec *, char *error, size_t size);
void scene_retain(scene *);
void scene_free(scene *);
const char *scene_name(const scene *);
const char *scene_ui(const scene *);
bool scene_perspective(const scene *);
physics_settings scene_physics(const scene *);
int scene_object_count(const scene *);
const char *scene_object_name(const scene *, int);
/* The object's parent index, or -1 for a root. */
int scene_object_parent(const scene *, int);
/* Whether every object descends from the first, as a prototype's do. */
bool scene_is_tree(const scene *);
int scene_link_count(const scene *);
scene_link scene_link_at(const scene *, int);
int scene_relationship_count(const scene *);
const ecs_relationship_desc *scene_relationship_at(const scene *, int);
int scene_pair_count(const scene *);
scene_pair scene_pair_at(const scene *, int);
int scene_object_behavior_count(const scene *, int);
const char *scene_object_behavior(const scene *, int object, int behavior);
/* Retained arena capacity plus the owning generation, for package budgets. */
size_t scene_storage_bytes(const scene *);
/* Borrowed immutable component payloads/descriptors until scene release. */
int scene_object_component_count(const scene *, int object);
scene_component scene_object_component_at(const scene *, int object, int component);
/* Explicit schema installation, outside ECS iteration and pending commands. */
int scene_register_components(const scene *, ecs_world *, char *error, size_t size);
/* Components must already be installed. Takes a scene reference and borrows
 * world/registry until free. Each child is ChildOf its parent's entity, and
 * each link holds its target's. All entities/components exist before any
 * behavior is attached; Begin remains deferred until the host's
 * initialization boundary. Failure removes created entities and cancels their
 * attachments. */
scene_instance *scene_instantiate(scene *, ecs_world *, source_registry *, char *error, size_t size);
/* Allocate component snapshots without attaching behavior modules. The caller
 * may initialize them and attach behavior at its normal initialization boundary. */
scene_instance *scene_instantiate_components(scene *, ecs_world *, char *error, size_t size);
/* Consume a component-only instance whose objects all descend from the first,
 * transferring entity teardown to the caller: destroying the first entity
 * destroys the rest. Returns the entities in object order; the caller afrees
 * the array. Their managed components remain owned by the ECS world. */
ecs_entity *scene_instance_take_entities(scene_instance *);
/* Convenience for an instance known to be releasable. Use the fallible API
 * when entities outside the instance can hold Panic links to its entities. */
void scene_instance_free(scene_instance *);
/* On refusal, the instance and all its entities/relationships remain intact. */
bool scene_instance_release(scene_instance *, char *error, size_t error_size);
ecs_entity scene_instance_entity(const scene_instance *, int);
/* Borrowed immutable scene retained by the instance. */
const scene *scene_instance_scene(const scene_instance *);

#endif
