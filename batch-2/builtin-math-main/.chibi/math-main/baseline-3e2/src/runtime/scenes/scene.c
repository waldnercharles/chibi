#include "runtime/scenes/scene.h"

#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct scene
{
    int references;
    arena storage;
    scene_spec spec;
};
struct scene_instance
{
    scene *scene;
    ecs_world *world;
    source_registry *sources;
    ecs_entity *entities;
};
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static const char *own(arena *storage, const char *text)
{
    if (!text) {
        return NULL;
    }
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(storage, size);
    memcpy(copy, text, size);
    return copy;
}
static bool same_name(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
static bool same_component(const ecs_component_desc *a, const ecs_component_desc *b)
{
    return a->id == b->id && a->element_size == b->element_size &&
           same_name(a->key, b->key) && same_name(a->name, b->name) &&
           a->index == b->index &&
           (a->type == b->type || ecs_layout_hash(a->type) == ecs_layout_hash(b->type));
}
static bool same_relationship(const ecs_relationship_desc *a, const ecs_relationship_desc *b)
{
    return a->id == b->id && same_name(a->name, b->name) &&
           same_name(a->key, b->key) && a->traits == b->traits &&
           a->on_delete_target == b->on_delete_target;
}
static bool prepare_relationships(scene *scene, const scene_spec *spec, char *error, size_t size)
{
    if (spec->relationship_count < 0 || spec->relationship_count > 1024 ||
        spec->pair_count < 0 || spec->pair_count > 1024 ||
        (spec->relationship_count && !spec->relationships) ||
        (spec->pair_count && !spec->pairs)) {
        return fail(error, size, "invalid prepared relationship count or storage");
    }
    ecs_relationship_desc *relationships = arena_alloc(
        &scene->storage,
        (size_t)spec->relationship_count * sizeof *relationships
    );
    scene->spec.relationships = relationships;
    scene_pair *pairs = arena_alloc(&scene->storage, (size_t)spec->pair_count * sizeof *pairs);
    scene->spec.pairs = pairs;
    if (!spec->relationship_count) {
        return !spec->pair_count ||
               fail(error, size, "prepared pairs have no relationship declarations");
    }
    ecs_world *world = ecs_world_new();
    bool valid = true;
    for (int i = 0; valid && i < spec->relationship_count; i++) {
        ecs_relationship_desc desc = spec->relationships[i];
        valid = desc.id && desc.name && desc.name[0] && desc.key && desc.key[0] &&
                !(desc.traits &
                  ~(ECS_REL_EXCLUSIVE | ECS_REL_ACYCLIC | ECS_REL_TRAVERSABLE)) &&
                desc.on_delete_target >= ECS_CLEANUP_REMOVE &&
                desc.on_delete_target <= ECS_CLEANUP_PANIC;
        if (desc.traits & ECS_REL_TRAVERSABLE) {
            desc.traits |= ECS_REL_ACYCLIC;
        }
        for (int j = 0; valid && j < i; j++) {
            valid = strcmp(desc.name, relationships[j].name) &&
                    strcmp(desc.key, relationships[j].key);
        }
        for (int j = 0; valid && j < spec->object_count; j++) {
            for (int k = 0; valid && k < spec->objects[j].component_count; k++) {
                const ecs_component_desc
                    *component = spec->objects[j].components[k].desc;
                valid = desc.id != component->id &&
                        !same_name(desc.name, component->name) &&
                        !same_name(desc.key, component->key);
            }
        }
        valid = valid && !ecs_world_register_relationship(world, &desc, NULL);
        relationships[i] = desc;
        relationships[i].name = own(&scene->storage, desc.name);
        relationships[i].key = own(&scene->storage, desc.key);
    }
    ecs_entity *entities = NULL;
    for (int i = 0; valid && spec->pair_count && i < spec->object_count; i++) {
        apush(entities, ecs_world_create_entity(world));
    }
    for (int i = 0; valid && i < spec->pair_count; i++) {
        scene_pair pair = spec->pairs[i];
        valid = pair.object >= 0 && pair.object < spec->object_count &&
                pair.target >= 0 && pair.target < spec->object_count &&
                pair.relationship >= 0 &&
                pair.relationship < spec->relationship_count;
        for (int j = 0; valid && j < i; j++) {
            scene_pair previous = pairs[j];
            if (previous.object == pair.object &&
                previous.relationship == pair.relationship &&
                (previous.target == pair.target ||
                 (relationships[pair.relationship].traits & ECS_REL_EXCLUSIVE))) {
                valid = false;
            }
        }
        if (valid) {
            valid = !ecs_world_record_add_pair(
                world,
                entities[pair.object],
                relationships[pair.relationship].id,
                entities[pair.target],
                (uint64_t)i
            );
        }
        pairs[i] = pair;
    }
    valid = valid && !ecs_world_apply_commands(world);
    afree(entities);
    ecs_world_free(world);
    return valid ||
           fail(error, size, "invalid prepared relationship identity, target or cycle");
}
typedef struct
{
    const ecs_component_desc *desc;
    int object;
} component_row;
enum
{
    COMPONENT_ID,
    COMPONENT_NAME,
    COMPONENT_KEY
};
static int compare_id(const void *left, const void *right)
{
    const component_row *a = left, *b = right;
    if (a->desc->id != b->desc->id) {
        return a->desc->id < b->desc->id ? -1 : 1;
    }
    return (a->object > b->object) - (a->object < b->object);
}
static int compare_name(const void *left, const void *right)
{
    const component_row *a = left, *b = right;
    int order = strcmp(a->desc->name, b->desc->name);
    return order ? order : (a->object > b->object) - (a->object < b->object);
}
static int compare_key(const void *left, const void *right)
{
    const component_row *a = left, *b = right;
    const char *x = a->desc->key, *y = b->desc->key;
    int order = x && y ? strcmp(x, y) : (x != NULL) - (y != NULL);
    return order ? order : (a->object > b->object) - (a->object < b->object);
}
static bool row_identity(const ecs_component_desc *a, const ecs_component_desc *b, int kind)
{
    switch (kind) {
        case COMPONENT_ID: return a->id == b->id;
        case COMPONENT_NAME: return same_name(a->name, b->name);
        case COMPONENT_KEY:
            return a->key && b->key && same_name(a->key, b->key);
    }
    assert(false);
    return false;
}
/* Sorting each identity groups conflicts without scanning all prior objects. */
static bool rows_valid(component_row *rows, int kind, char *error, size_t size)
{
    int first = 0;
    for (int i = 1; i < acount(rows); i++) {
        if (!row_identity(rows[first].desc, rows[i].desc, kind)) {
            first = i;
            continue;
        }
        if (rows[i - 1].object == rows[i].object) {
            return fail(error, size, "prepared scene object component is duplicated");
        }
        if (!same_component(rows[first].desc, rows[i].desc)) {
            return fail(error, size, "prepared scene component identity has conflicting layouts");
        }
    }
    return true;
}
static bool link_valid(const scene_spec *spec, scene_link link)
{
    if (link.object < 0 || link.object >= spec->object_count ||
        link.target < 0 || link.target >= spec->object_count) {
        return false;
    }
    const scene_object *object = &spec->objects[link.object];
    if (link.component < 0 || link.component >= object->component_count) {
        return false;
    }
    const type_desc *type = object->components[link.component].desc->type;
    return link.field >= 0 && link.field < type->field_count &&
           type->fields[link.field].type == PIN_HANDLE;
}
scene *scene_prepare(const scene_spec *spec, char *error, size_t size)
{
    assert(spec && (!size || error));
    if (size) {
        error[0] = '\0';
    }
    if (!spec->name || !spec->name[0] || spec->object_count < 0 ||
        (spec->object_count && !spec->objects) ||
        !physics_settings_valid(spec->physics)) {
        fail(error, size, "invalid prepared scene metadata");
        return NULL;
    }
    component_row *rows = NULL;
    scene *scene = xcalloc(1, sizeof *scene);
    scene->references = 1;
    scene->spec = *spec;
    scene->spec.name = own(&scene->storage, spec->name);
    scene->spec.ui = own(&scene->storage, spec->ui);
    scene_object *objects = arena_alloc(&scene->storage, (size_t)spec->object_count * sizeof *objects);
    scene->spec.objects = objects;
    int *parents = arena_alloc(&scene->storage, (size_t)spec->object_count * sizeof *parents);
    scene->spec.parents = parents;
    /* Repeated rows often share a type. Keep this memo within preparation:
     * a later call may resolve its nested types through a different catalog. */
    const type_desc *checked_type = NULL;
    uint64_t checked_layout = 0;
    for (int i = 0; i < spec->object_count; i++) {
        const scene_object *input = &spec->objects[i];
        parents[i] = spec->parents ? spec->parents[i] : -1;
        if (parents[i] < -1 || parents[i] >= i) {
            fail(error, size, "prepared scene object parent must precede it");
            goto failed;
        }
        if (!input->name || !input->name[0] || input->component_count < 0 ||
            input->behavior_count < 0 ||
            (input->component_count && !input->components) ||
            (input->behavior_count && !input->behaviors)) {
            fail(error, size, "invalid prepared scene object");
            goto failed;
        }
        for (int j = 0; j < i; j++) {
            if (parents[j] == parents[i] && !strcmp(objects[j].name, input->name)) {
                fail(error, size, "prepared scene object name is duplicated");
                goto failed;
            }
        }
        objects[i] = *input;
        objects[i].name = own(&scene->storage, input->name);
        scene_component *components = arena_alloc(
            &scene->storage,
            (size_t)input->component_count * sizeof *components
        );
        objects[i].components = components;
        for (int j = 0; j < input->component_count; j++) {
            const scene_component *component = &input->components[j];
            const ecs_component_desc *desc = component->desc;
            if (!desc || !desc->id || !desc->name || !desc->type || !desc->element_size ||
                desc->type->size != desc->element_size || !component->data) {
                fail(error, size, "invalid prepared scene component");
                goto failed;
            }
            if (desc->derived_from) {
                /* Propagation computes it from the component it derives from. */
                fail(error, size, "prepared scene component is derived, not authored");
                goto failed;
            }
            if (desc->layout_hash) {
                if (checked_type != desc->type) {
                    checked_type = desc->type;
                    checked_layout = ecs_layout_hash(desc->type);
                }
                if (desc->layout_hash != checked_layout) {
                    fail(error, size, "prepared scene component layout hash differs");
                    goto failed;
                }
            }
            ecs_component_desc *copy = arena_alloc(&scene->storage, sizeof *copy);
            *copy = *desc;
            copy->name = own(&scene->storage, desc->name);
            copy->key = own(&scene->storage, desc->key);
            copy->column_base = NULL;
            void *data = arena_alloc(&scene->storage, desc->element_size);
            memcpy(data, component->data, desc->element_size);
            if (pin_struct_snapshot_fields(&scene->storage, desc->type, data)) {
                fail(error, size, "prepared scene component payload cannot be snapshotted");
                goto failed;
            }
            type_validate_ranges(desc->type, data);
            components[j] = (scene_component){ copy, data };
            apush(rows, ((component_row){ copy, i }));
        }
        const char **behaviors = arena_alloc(
            &scene->storage,
            (size_t)input->behavior_count * sizeof *behaviors
        );
        objects[i].behaviors = behaviors;
        for (int j = 0; j < input->behavior_count; j++) {
            if (!input->behaviors[j] || !input->behaviors[j][0]) {
                fail(error, size, "prepared scene behavior name is missing");
                goto failed;
            }
            for (int k = 0; k < j; k++) {
                if (!strcmp(behaviors[k], input->behaviors[j])) {
                    fail(error, size, "prepared scene behavior name is duplicated");
                    goto failed;
                }
            }
            behaviors[j] = own(&scene->storage, input->behaviors[j]);
        }
    }
    if (acount(rows) > 1) {
        qsort(rows, (size_t)acount(rows), sizeof *rows, compare_id);
        if (!rows_valid(rows, COMPONENT_ID, error, size)) {
            goto failed;
        }
        qsort(rows, (size_t)acount(rows), sizeof *rows, compare_name);
        if (!rows_valid(rows, COMPONENT_NAME, error, size)) {
            goto failed;
        }
        qsort(rows, (size_t)acount(rows), sizeof *rows, compare_key);
        if (!rows_valid(rows, COMPONENT_KEY, error, size)) {
            goto failed;
        }
    }
    afree(rows);
    if (spec->link_count < 0 || (spec->link_count && !spec->links)) {
        fail(error, size, "invalid prepared scene links");
        goto failed;
    }
    scene_link *links = arena_alloc(&scene->storage, (size_t)spec->link_count * sizeof *links);
    scene->spec.links = links;
    for (int i = 0; i < spec->link_count; i++) {
        if (!link_valid(&scene->spec, spec->links[i])) {
            fail(error, size, "prepared scene link names no handle field or object");
            goto failed;
        }
        links[i] = spec->links[i];
    }
    if (!prepare_relationships(scene, spec, error, size)) {
        goto failed;
    }
    return scene;
failed:
    afree(rows);
    scene_free(scene);
    return NULL;
}
void scene_retain(scene *scene)
{
    assert(scene && scene->references > 0);
    scene->references++;
}
void scene_free(scene *scene)
{
    if (!scene || --scene->references) {
        return;
    }
    arena_free(&scene->storage);
    free(scene);
}
const char *scene_name(const scene *scene)
{
    return scene->spec.name;
}
const char *scene_ui(const scene *scene)
{
    return scene->spec.ui;
}
bool scene_perspective(const scene *scene)
{
    return scene->spec.perspective;
}
physics_settings scene_physics(const scene *scene)
{
    return scene->spec.physics;
}
int scene_object_count(const scene *scene)
{
    return scene->spec.object_count;
}
const char *scene_object_name(const scene *scene, int index)
{
    assert(index >= 0 && index < scene->spec.object_count);
    return scene->spec.objects[index].name;
}
int scene_object_parent(const scene *scene, int index)
{
    assert(index >= 0 && index < scene->spec.object_count);
    return scene->spec.parents[index];
}
bool scene_is_tree(const scene *scene)
{
    for (int i = 1; i < scene->spec.object_count; i++) {
        if (scene->spec.parents[i] < 0) {
            return false;
        }
    }
    return scene->spec.object_count > 0;
}
size_t scene_storage_bytes(const scene *scene)
{
    size_t capacity = arena_storage_bytes(&scene->storage);
    assert(capacity <= SIZE_MAX - sizeof *scene);
    return capacity + sizeof *scene;
}
int scene_object_component_count(const scene *scene, int object)
{
    assert(object >= 0 && object < scene->spec.object_count);
    return scene->spec.objects[object].component_count;
}
scene_component scene_object_component_at(const scene *scene, int object, int component)
{
    assert(object >= 0 && object < scene->spec.object_count);
    assert(component >= 0 && component < scene->spec.objects[object].component_count);
    return scene->spec.objects[object].components[component];
}
static bool validate_world(const scene *scene, ecs_world *world, bool installed, char *error, size_t size)
{
    for (int i = 0; i < scene->spec.relationship_count; i++) {
        const ecs_relationship_desc *desc = &scene->spec.relationships[i];
        const ecs_relationship_desc *found = ecs_world_relationship(world, desc->id);
        if ((found && !same_relationship(desc, found)) ||
            (installed && !found) || ecs_world_component(world, desc->id)) {
            return fail(error, size, "prepared relationship schema differs from the world");
        }
    }
    for (int i = 0; i < scene->spec.object_count; i++) {
        const scene_object *object = &scene->spec.objects[i];
        for (int j = 0; j < object->component_count; j++) {
            const ecs_component_desc *desc = object->components[j].desc;
            const ecs_component_desc *found = ecs_world_component(world, desc->id);
            if ((found && !same_component(desc, found)) || (installed && !found)) {
                return fail(error, size, "prepared scene component schema differs from the world");
            }
            for (int k = 0; k < ecs_world_component_count(world); k++) {
                const ecs_component_desc *previous = ecs_world_component_at(world, k);
                if (previous->id != desc->id &&
                    (same_name(previous->name, desc->name) ||
                     (previous->key && desc->key &&
                      same_name(previous->key, desc->key)))) {
                    return fail(error, size, "prepared scene component name has a conflicting identity");
                }
            }
        }
    }
    return true;
}
int scene_register_components(const scene *scene, ecs_world *world, char *error, size_t size)
{
    assert(scene && world && (!size || error) && !ecs_world_command_count(world));
    if (!validate_world(scene, world, false, error, size)) {
        return -1;
    }
    for (int i = 0; i < scene->spec.relationship_count; i++) {
        const ecs_relationship_desc *desc = &scene->spec.relationships[i];
        if (!ecs_world_relationship(world, desc->id) &&
            ecs_world_register_relationship(world, desc, NULL)) {
            fail(error, size, "prepared relationship could not be registered");
            return -1;
        }
    }
    for (int i = 0; i < scene->spec.object_count; i++) {
        const scene_object *object = &scene->spec.objects[i];
        for (int j = 0; j < object->component_count; j++) {
            const ecs_component_desc *desc = object->components[j].desc;
            if (!ecs_world_component(world, desc->id) &&
                ecs_world_register_component(world, desc, NULL)) {
                fail(error, size, "prepared scene component could not be registered");
                return -1;
            }
        }
    }
    return 0;
}
bool scene_instance_release(scene_instance *instance, char *error, size_t size)
{
    if (!instance) {
        return true;
    }
    if (ecs_world_prepare_owned_destruction(
            instance->world,
            instance->entities,
            acount(instance->entities)
        )) {
        fail(error, size, "scene release requires a quiescent world without external Panic links");
        return false;
    }
    /* Children go before their parents, whose destruction would take them. */
    for (int i = acount(instance->entities) - 1; i >= 0; i--) {
        ecs_entity entity = instance->entities[i];
        if (instance->sources) {
            source_detach_entity(instance->sources, entity);
        }
        if (ecs_entity_is_alive(instance->world, entity)) {
            int status = ecs_world_destroy_entity(instance->world, entity);
            assert(status == ECS_OK);
            (void)status;
        }
    }
    afree(instance->entities);
    scene_free(instance->scene);
    free(instance);
    return true;
}
void scene_instance_free(scene_instance *instance)
{
    bool released = scene_instance_release(instance, NULL, 0);
    assert(released);
    (void)released;
}
static scene_instance *instantiate(
    scene *scene,
    ecs_world *world,
    source_registry *sources,
    bool attach,
    char *error,
    size_t size
)
{
    assert(scene && world && (!size || error) && !ecs_world_command_count(world));
    assert(!sources || source_world(sources) == world);
    if (size) {
        error[0] = '\0';
    }
    if (!validate_world(scene, world, true, error, size)) {
        return NULL;
    }
    for (int i = 0; i < scene->spec.object_count; i++) {
        const scene_object *object = &scene->spec.objects[i];
        for (int j = 0; attach && j < object->behavior_count; j++) {
            if (!sources || !source_registered(sources, object->behaviors[j])) {
                fail(error, size, "prepared scene behavior module is not registered");
                return NULL;
            }
        }
    }
    scene_instance *instance = xcalloc(1, sizeof *instance);
    instance->scene = scene;
    instance->world = world;
    instance->sources = sources;
    scene_retain(scene);
    for (int i = 0; i < scene->spec.object_count; i++) {
        ecs_entity entity = ecs_world_record_create(world, (uint64_t)i);
        apush(instance->entities, entity);
        if (!entity.slot) {
            goto failed;
        }
    }
    if (ecs_world_apply_commands(world)) {
        goto failed;
    }
    for (int i = 0; i < scene->spec.object_count; i++) {
        ecs_entity entity = instance->entities[i];
        const scene_object *object = &scene->spec.objects[i];
        for (int j = 0; j < object->component_count; j++) {
            if (ecs_world_record_add(world, entity, object->components[j].desc->id, NULL, (uint64_t)i)) {
                goto failed;
            }
        }
        int parent = scene->spec.parents[i];
        if (parent >= 0 &&
            ecs_world_record_add_pair(world, entity, ECS_CHILD_OF, instance->entities[parent], (uint64_t)i)) {
            goto failed;
        }
    }
    if (ecs_world_apply_commands(world)) {
        goto failed;
    }
    for (int i = 0; i < scene->spec.object_count; i++) {
        const scene_object *object = &scene->spec.objects[i];
        ecs_entity entity = instance->entities[i];
        for (int j = 0; j < object->component_count; j++) {
            const scene_component *component = &object->components[j];
            pin_value value = { .type = PIN_STRUCT,
                                .type_name = component->desc->type->name,
                                .ptr = (void *)component->data };
            if (ecs_component_write_value(world, entity, component->desc->id, 0, value, value)) {
                goto failed;
            }
        }
    }
    for (int i = 0; i < scene->spec.pair_count; i++) {
        scene_pair pair = scene->spec.pairs[i];
        if (ecs_world_record_add_pair(
                world,
                instance->entities[pair.object],
                scene->spec.relationships[pair.relationship].id,
                instance->entities[pair.target],
                (uint64_t)i
            )) {
            goto failed;
        }
    }
    if (scene->spec.pair_count && ecs_world_apply_commands(world)) {
        goto failed;
    }
    for (int i = 0; i < scene->spec.link_count; i++) {
        scene_link link = scene->spec.links[i];
        const ecs_component_desc *desc = scene->spec.objects[link.object]
                                             .components[link.component]
                                             .desc;
        pin_value value = { .type = PIN_HANDLE,
                            .type_name = desc->type->fields[link.field].type_name,
                            .u64 = ecs_entity_to_u64(instance->entities[link.target]) };
        if (ecs_component_write_field(world, instance->entities[link.object], desc->id, link.field, value)) {
            goto failed;
        }
    }
    for (int i = 0; i < scene->spec.object_count; i++) {
        const scene_object *object = &scene->spec.objects[i];
        for (int j = 0; attach && j < object->behavior_count; j++) {
            if (source_attach(sources, instance->entities[i], object->behaviors[j], true, 0, 0)) {
                goto failed;
            }
        }
    }
    return instance;
failed:
    ecs_world_clear_commands(world);
    scene_instance_free(instance);
    fail(error, size, "prepared scene could not instantiate");
    return NULL;
}
const scene *scene_instance_scene(const scene_instance *instance)
{
    assert(instance);
    return instance->scene;
}
ecs_entity scene_instance_entity(const scene_instance *instance, int index)
{
    assert(index >= 0 && index < acount(instance->entities));
    return instance->entities[index];
}

int scene_link_count(const scene *scene)
{
    return scene->spec.link_count;
}
scene_link scene_link_at(const scene *scene, int index)
{
    assert(index >= 0 && index < scene->spec.link_count);
    return scene->spec.links[index];
}
int scene_relationship_count(const scene *scene)
{
    return scene->spec.relationship_count;
}
const ecs_relationship_desc *scene_relationship_at(const scene *scene, int index)
{
    assert(index >= 0 && index < scene->spec.relationship_count);
    return &scene->spec.relationships[index];
}
int scene_pair_count(const scene *scene)
{
    return scene->spec.pair_count;
}
scene_pair scene_pair_at(const scene *scene, int index)
{
    assert(index >= 0 && index < scene->spec.pair_count);
    return scene->spec.pairs[index];
}
int scene_object_behavior_count(const scene *scene, int index)
{
    assert(index >= 0 && index < scene->spec.object_count);
    return scene->spec.objects[index].behavior_count;
}
const char *scene_object_behavior(const scene *scene, int object, int behavior)
{
    assert(object >= 0 && object < scene->spec.object_count);
    assert(behavior >= 0 && behavior < scene->spec.objects[object].behavior_count);
    return scene->spec.objects[object].behaviors[behavior];
}
scene_instance *scene_instantiate(
    scene *scene,
    ecs_world *world,
    source_registry *sources,
    char *error,
    size_t size
)
{
    return instantiate(scene, world, sources, true, error, size);
}
scene_instance *scene_instantiate_components(scene *scene, ecs_world *world, char *error, size_t size)
{
    return instantiate(scene, world, NULL, false, error, size);
}
ecs_entity *scene_instance_take_entities(scene_instance *instance)
{
    assert(instance && !instance->sources && scene_is_tree(instance->scene));
    ecs_entity *entities = instance->entities;
    instance->entities = NULL;
    scene_instance_free(instance);
    return entities;
}
